uniform sampler2D u_Texture0; 
uniform sampler2D u_Texture1; 
uniform sampler2D u_Texture2; 
uniform sampler2D u_Texture3; 
uniform sampler2D u_Texture4; 
uniform sampler2D u_Texture5; 
uniform sampler2D u_Texture6; 

varying vec2 texture_coordinate;

uniform float u_ScreenToNextPixelX;
uniform float u_ScreenToNextPixelY;
uniform float u_ScreenSizeX;
uniform float u_ScreenSizeY;
uniform float u_MotionBlurX;
uniform float u_MotionBlurY;

// 	This is a simple motion blur shader


float Blurs[3] = 	{17.6, 7.7, 0}; // to right and
int Passes = 32;

void main()
{
	
    	// Sampling The Texture And Passing It To The Frame Buffer 
	gl_FragColor = texture2D(u_Texture0, texture_coordinate); 

	// Actually....

	// Blend x4 (60 = 240fps)
	vec4 m2 = texture2D(u_mpass1, texture_coordinate); 
	vec4 m3 = texture2D(u_mpass2, texture_coordinate); 
	vec4 m4 = texture2D(u_mpass3, texture_coordinate); 
	vec4 m5 = texture2D(u_mpass4, texture_coordinate); 
	vec4 blur = (m2 + m3 + m4 + m5) / 4;

	// Not yet implemented
/*
	// Blend x3 (60 = 180fps)
	vec4 m2 = texture2D(u_mpass1, texture_coordinate); 
	vec4 m3 = texture2D(u_mpass2, texture_coordinate); 
	vec4 m4 = texture2D(u_mpass3, texture_coordinate); 
	vec4 blur = (m2 + m3 + m4) / 3;


	// Blend x2 (60 = 120fps)

	vec4 m2 = texture2D(u_mpass1, texture_coordinate); 
	vec4 m3 = texture2D(u_mpass2, texture_coordinate); 
	vec4 blur = (m2 + m3) / 2;
*/
	gl_FragColor = blur;
//	gl_FragColor.b *= 3.1;	// this is to check if my shader is working.
	
}	