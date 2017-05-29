in vec4 pixelpos;
in vec3 glowdist;

in vec4 vWorldNormal;
in vec4 vEyeNormal;
in vec4 vTexCoord;
in vec4 vColor;

out vec4 FragColor;
#ifdef GBUFFER_PASS
out vec4 FragFog;
out vec4 FragNormal;
#endif

#ifdef SHADER_STORAGE_LIGHTS
	layout(std430, binding = 1) buffer LightBufferSSO
	{
		vec4 lights[];
	};
#elif defined NUM_UBO_LIGHTS
	/*layout(std140)*/ uniform LightBufferUBO
	{
		vec4 lights[NUM_UBO_LIGHTS];
	};
#endif


uniform sampler2D tex;
uniform sampler2D ShadowMap;

vec4 Process(vec4 color);
vec4 ProcessTexel();
vec4 ProcessLight(vec4 color);


//===========================================================================
//
// Desaturate a color
//
//===========================================================================

vec4 desaturate(vec4 texel)
{
	if (uDesaturationFactor > 0.0)
	{
		float gray = (texel.r * 0.3 + texel.g * 0.56 + texel.b * 0.14);	
		return mix (texel, vec4(gray,gray,gray,texel.a), uDesaturationFactor);
	}
	else
	{
		return texel;
	}
}

//===========================================================================
//
// This function is common for all (non-special-effect) fragment shaders
//
//===========================================================================

vec4 getTexel(vec2 st)
{
	vec4 texel = texture(tex, st);
	
	//
	// Apply texture modes
	//
	switch (uTextureMode)
	{
		case 1:	// TM_MASK
			texel.rgb = vec3(1.0,1.0,1.0);
			break;
			
		case 2:	// TM_OPAQUE
			texel.a = 1.0;
			break;
			
		case 3:	// TM_INVERSE
			texel = vec4(1.0-texel.r, 1.0-texel.b, 1.0-texel.g, texel.a);
			break;
			
		case 4:	// TM_REDTOALPHA
			texel = vec4(1.0, 1.0, 1.0, texel.r*texel.a);
			break;
			
		case 5:	// TM_CLAMPY
			if (st.t < 0.0 || st.t > 1.0)
			{
				texel.a = 0.0;
			}
			break;
	}
	if (uObjectColor2.a == 0.0) texel *= uObjectColor;
	else texel *= mix(uObjectColor, uObjectColor2, glowdist.z);

	return desaturate(texel);
}

//===========================================================================
//
// Doom lighting equation exactly as calculated by zdoom.
//
//===========================================================================
float R_DoomLightingEquation(float light)
{
	float TotalLight = light * 255.0;

	//Spooky house Light Equation
	float lightscale = 1.0;
	//light/33 should be 4 whe light is 130
	float MaxFullLightDist = (TotalLight/33) * TotalLight;
	float tmp=0;
	if(TotalLight > 0){//sector light is zero so there'll be no light
		if(TotalLight == 255){ //if the sector is full light, it'll always be full light
			lightscale = 0;
		}
		else{
			tmp = min(pixelpos.w / 10.0, 64.0 + MaxFullLightDist + (TotalLight/10));
			tmp = (tmp / (64.0 + MaxFullLightDist  + (TotalLight/10) ) )* 10 ;
			lightscale = clamp(tmp, 0 ,1.0);
		}
		else{			
			tmp = min(pixelpos.w / 10.0, 64.0 + MaxFullLightDist + (TotalLight/10));
			tmp = (tmp / (64.0 + MaxFullLightDist  + (TotalLight/10) ) )* 10 ;
			lightscale = clamp(tmp, 0 ,1.0);
		}
	}
	return lightscale;
}

//===========================================================================
//
// Check if light is in shadow according to its 1D shadow map
//
//===========================================================================

#ifdef SUPPORTS_SHADOWMAPS

float sampleShadowmap(vec2 dir, float v)
{
	float u;
	if (abs(dir.x) > abs(dir.y))
	{
		if (dir.x >= 0.0)
			u = dir.y / dir.x * 0.125 + (0.25 + 0.125);
		else
			u = dir.y / dir.x * 0.125 + (0.75 + 0.125);
	}
	else
	{
		if (dir.y >= 0.0)
			u = dir.x / dir.y * 0.125 + 0.125;
		else
			u = dir.x / dir.y * 0.125 + (0.50 + 0.125);
	}
	float dist2 = dot(dir, dir);
	return texture(ShadowMap, vec2(u, v)).x > dist2 ? 1.0 : 0.0;
}

//===========================================================================
//
// Check if light is in shadow using Percentage Closer Filtering (PCF)
//
//===========================================================================

#define PCF_FILTER_STEP_COUNT 3
#define PCF_COUNT (PCF_FILTER_STEP_COUNT * 2 + 1)

float shadowmapAttenuation(vec4 lightpos, float shadowIndex)
{
	if (shadowIndex >= 1024.0)
		return 1.0; // No shadowmap available for this light

	float v = (shadowIndex + 0.5) / 1024.0;

	vec2 ray = pixelpos.xz - lightpos.xz;
	float length = length(ray);
	if (length < 3.0)
		return 1.0;

	vec2 dir = ray / length;

	ray -= dir * 2.0; // margin
	dir = dir * min(length / 50.0, 1.0); // avoid sampling behind light

	vec2 normal = vec2(-dir.y, dir.x);
	vec2 bias = dir * 10.0;

	float sum = 0.0;
	for (float x = -PCF_FILTER_STEP_COUNT; x <= PCF_FILTER_STEP_COUNT; x++)
	{
		sum += sampleShadowmap(ray + normal * x - bias * abs(x), v);
	}
	return sum / PCF_COUNT;
}

#endif

//===========================================================================
//
// Standard lambertian diffuse light calculation
//
//===========================================================================

float diffuseContribution(vec3 lightDirection, vec3 normal)
{
	return max(dot(normal, lightDirection), 0.0f);
}

//===========================================================================
//
// Calculates the brightness of a dynamic point light
// Todo: Find a better way to define which lighting model to use.
// (Specular mode has been removed for now.)
//
//===========================================================================

float pointLightAttenuation(vec4 lightpos, float lightcolorA)
{
	float attenuation = max(lightpos.w - distance(pixelpos.xyz, lightpos.xyz),0.0) / lightpos.w;
#ifdef SUPPORTS_SHADOWMAPS
	float shadowIndex = abs(lightcolorA) - 1.0;
	attenuation *= shadowmapAttenuation(lightpos, shadowIndex);
#endif
	if (lightcolorA >= 0.0) // Sign bit is the attenuated light flag
	{
		return attenuation;
	}
	else
	{
		vec3 lightDirection = normalize(lightpos.xyz - pixelpos.xyz);
		float diffuseAmount = diffuseContribution(lightDirection, normalize(vWorldNormal.xyz));
		return attenuation * diffuseAmount;
	}
}

//===========================================================================
//
// Calculate light
//
// It is important to note that the light color is not desaturated
// due to ZDoom's implementation weirdness. Everything that's added
// on top of it, e.g. dynamic lights and glows are, though, because
// the objects emitting these lights are also.
//
// This is making this a bit more complicated than it needs to
// because we can't just desaturate the final fragment color.
//
//===========================================================================

vec4 getLightColor(float fogdist, float fogfactor)
{
	vec4 color = vColor;
	
	if (uLightLevel >= 0.0)
	{
		float newlightlevel = 1.0 - R_DoomLightingEquation(uLightLevel);
		color.rgb *= newlightlevel;
	}
	else if (uFogEnabled > 0)
	{
		// brightening around the player for light mode 2
		if (fogdist < uLightDist)
		{
			color.rgb *= uLightFactor - (fogdist / uLightDist) * (uLightFactor - 1.0);
		}
		
		//
		// apply light diminishing through fog equation
		//
		color.rgb = mix(vec3(0.0, 0.0, 0.0), color.rgb, fogfactor);
	}
	
	//
	// handle glowing walls
	//
	if (uGlowTopColor.a > 0.0 && glowdist.x < uGlowTopColor.a)
	{
		color.rgb += desaturate(uGlowTopColor * (1.0 - glowdist.x / uGlowTopColor.a)).rgb;
	}
	if (uGlowBottomColor.a > 0.0 && glowdist.y < uGlowBottomColor.a)
	{
		color.rgb += desaturate(uGlowBottomColor * (1.0 - glowdist.y / uGlowBottomColor.a)).rgb;
	}
	color = min(color, 1.0);

	//
	// apply brightmaps (or other light manipulation by custom shaders.
	//
	color = ProcessLight(color);

	//
	// apply dynamic lights (except additive)
	//
	
	vec4 dynlight = uDynLightColor;

#if defined NUM_UBO_LIGHTS || defined SHADER_STORAGE_LIGHTS
	if (uLightIndex >= 0)
	{
		ivec4 lightRange = ivec4(lights[uLightIndex]) + ivec4(uLightIndex + 1);
		if (lightRange.z > lightRange.x)
		{
			//
			// modulated lights
			//
			for(int i=lightRange.x; i<lightRange.y; i+=2)
			{
				vec4 lightpos = lights[i];
				vec4 lightcolor = lights[i+1];
				
				lightcolor.rgb *= pointLightAttenuation(lightpos, lightcolor.a);
				dynlight.rgb += lightcolor.rgb;
			}
			//
			// subtractive lights
			//
			for(int i=lightRange.y; i<lightRange.z; i+=2)
			{
				vec4 lightpos = lights[i];
				vec4 lightcolor = lights[i+1];
				
				lightcolor.rgb *= pointLightAttenuation(lightpos, lightcolor.a);
				dynlight.rgb -= lightcolor.rgb;
			}
		}
	}
#endif
	color.rgb = clamp(color.rgb + desaturate(dynlight).rgb, 0.0, 1.4);
	
	// prevent any unintentional messing around with the alpha.
	return vec4(color.rgb, vColor.a);
}

//===========================================================================
//
// Applies colored fog
//
//===========================================================================

vec4 applyFog(vec4 frag, float fogfactor)
{
	return vec4(mix(uFogColor.rgb, frag.rgb, fogfactor), frag.a);
}

//===========================================================================
//
// The color of the fragment if it is fully occluded by ambient lighting
//
//===========================================================================

vec3 AmbientOcclusionColor()
{
	float fogdist;
	float fogfactor;
			
	//
	// calculate fog factor
	//
	if (uFogEnabled == -1) 
	{
		fogdist = pixelpos.w;
	}
	else 
	{
		fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
	}
	fogfactor = exp2 (uFogDensity * fogdist);
			
	return mix(uFogColor.rgb, vec3(0.0), fogfactor);
}

//===========================================================================
//
// Main shader routine
//
//===========================================================================

void main()
{
	vec4 frag = ProcessTexel();
	
#ifndef NO_ALPHATEST
	if (frag.a <= uAlphaThreshold) discard;
#endif

	switch (uFixedColormap)
	{
		case 0:
		{
			float fogdist = 0.0;
			float fogfactor = 0.0;
			

			
			//
			// calculate fog factor
			//
			if (uFogEnabled != 0)
			{
				if (uFogEnabled == 1 || uFogEnabled == -1) 
				{
					fogdist = pixelpos.w;
				}
				else 
				{
					fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
				}
				fogfactor = exp2 (uFogDensity * fogdist);
			}
			
			
			frag *= getLightColor(fogdist, fogfactor);
			
#if defined NUM_UBO_LIGHTS || defined SHADER_STORAGE_LIGHTS
			if (uLightIndex >= 0)
			{
				ivec4 lightRange = ivec4(lights[uLightIndex]) + ivec4(uLightIndex + 1);
				if (lightRange.w > lightRange.z)
				{
					vec4 addlight = vec4(0.0,0.0,0.0,0.0);
				
					//
					// additive lights - these can be done after the alpha test.
					//
					for(int i=lightRange.z; i<lightRange.w; i+=2)
					{
						vec4 lightpos = lights[i];
						vec4 lightcolor = lights[i+1];
						
						lightcolor.rgb *= pointLightAttenuation(lightpos, lightcolor.a);
						addlight.rgb += lightcolor.rgb;
					}
					frag.rgb = clamp(frag.rgb + desaturate(addlight).rgb, 0.0, 1.0);
				}
			}
#endif

			//
			// colored fog
			//
			if (uFogEnabled < 0) 
			{
				frag = applyFog(frag, fogfactor);
			}
			
			break;
		}
		
		case 1:
		{
			float gray = (frag.r * 0.3 + frag.g * 0.56 + frag.b * 0.14);	
			vec4 cm = uFixedColormapStart + gray * uFixedColormapRange;
			frag = vec4(clamp(cm.rgb, 0.0, 1.0), frag.a*vColor.a);
			break;
		}
		
		case 2:
		{
			frag = vColor * frag * uFixedColormapStart;
			break;
		}

		case 3:
		{
			float fogdist;
			float fogfactor;
			
			//
			// calculate fog factor
			//
			if (uFogEnabled == -1) 
			{
				fogdist = pixelpos.w;
			}
			else 
			{
				fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
			}
			fogfactor = exp2 (uFogDensity * fogdist);
			
			frag = vec4(uFogColor.rgb, (1.0 - fogfactor) * frag.a * 0.75 * vColor.a);
			break;
		}
	}
	FragColor = frag;
#ifdef GBUFFER_PASS
	FragFog = vec4(AmbientOcclusionColor(), 1.0);
	FragNormal = vec4(vEyeNormal.xyz * 0.5 + 0.5, 1.0);
#endif
}

