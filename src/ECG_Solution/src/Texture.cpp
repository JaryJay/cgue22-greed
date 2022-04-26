#include "Texture.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <thread>

/// @brief create a new texture, used in Framebuffer.h
/// @param type GL Texture type, eg GL_TEXTURE_2D
/// @param width of the texture (same as framebuffer)
/// @param height of the texture (same as framebuffer)
/// @param internalFormat is the color or depth format
Texture::Texture(GLenum type, int width, int height, GLenum internalFormat)
	: type_(type)
{
	glCreateTextures(type, 1, &tex_ID);
	glTextureParameteri(tex_ID, GL_TEXTURE_MAX_LEVEL, 0);
	glTextureParameteri(tex_ID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(tex_ID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureStorage2D(tex_ID, getNumMipMapLevels2D(width, height), internalFormat, width, height);
}

/// @brief loads a texture from image, used in Material.h
/// @param texPath is the location of an image
/// @return the created texture handle
GLuint Texture::loadTexture(const char* texPath)
{
	GLuint handle = 0;
	// generate texture
	glCreateTextures(GL_TEXTURE_2D, 1, &handle);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_set_flip_vertically_on_load(true);

	int w, h, comp;
	const uint8_t* img = stbi_load(texPath, &w, &h, &comp, 3);

	if (img > 0)
	{
		glTextureStorage2D(handle, 1, GL_RGB8, w, h);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTextureSubImage2D(handle, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, img);
		glBindTextures(0, 1, &handle);
		delete img;
	}
	else
	{
		std::cout << "could not load texture" << texPath << std::endl;
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	return handle;
}

/// @brief multi threaded variant
/// @param texPath 
/// @param handles 
void Texture::loadTextureMT(const char* texPath, GLuint handles[])
{
	stbiData imgData[5]; std::thread workers[5];

	std::string albedo = append(texPath, "/albedo.jpg");
	workers[0] = std::thread (Texture::stbiLoad, albedo, &imgData[0]);
	std::string normal = append(texPath, "/normal.jpg");
	workers[1] = std::thread (Texture::stbiLoad, normal, &imgData[1]);
	std::string metal = append(texPath, "/metal.jpg");
	workers[2] = std::thread (Texture::stbiLoad, metal, &imgData[2]);
	std::string rough = append(texPath, "/rough.jpg");
	workers[3] = std::thread (Texture::stbiLoad, rough, &imgData[3]);
	std::string ao = append(texPath, "/ao.jpg");
	workers[4] = std::thread (Texture::stbiLoad, ao, &imgData[4]);

	for (size_t i = 0; i < 5; i++)
	{
		workers[i].join();
		glCreateTextures(GL_TEXTURE_2D, 1, &handles[i]);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		
		if (imgData[i].data > 0)
		{
			glTextureStorage2D(handles[i], 1, GL_RGB8, imgData[i].w, imgData[i].h);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTextureSubImage2D(handles[i], 0, 0, 0, imgData[i].w, imgData[i].h, GL_RGB, GL_UNSIGNED_BYTE, imgData[i].data);
			glBindTextures(0, 1, &handles[i]);
			delete imgData[i].data;
		}
		else
		{
			std::cout << "could not load texture" << texPath << std::endl;
		}
	}
	glBindTexture(GL_TEXTURE_2D, 0);
}

/// @brief loads a texture from image, used in Material.h
/// @param texPath is the location of an image
/// @return the created texture handle
GLuint Texture::loadTextureTransparent(const char* texPath)
{
	GLuint handle = 0;
	// generate texture
	glCreateTextures(GL_TEXTURE_2D, 1, &handle);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_set_flip_vertically_on_load(true);

	int w, h, comp;
	const uint8_t* img = stbi_load(texPath, &w, &h, &comp, STBI_rgb_alpha);

	if (img > 0)
	{
		glTextureStorage2D(handle, 1, GL_RGBA8, w, h);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTextureSubImage2D(handle, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img);
		glBindTextures(0, 1, &handle);
		delete img;
	}
	else
	{
		std::cout << "could not load texture" << texPath << std::endl;
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	return handle;
}

/// @brief loads a 3dlut in .cube format, used for color grading in Renderer
/// code from https://svnte.se/3d-lut
/// @param texPath is the location of the lut
/// @return the created texture handle
GLuint Texture::load3Dlut(const char* texPath)
{
	// Load .CUBE file 
	printf("Loading LUT file %s \n", texPath);
	FILE* file = fopen(texPath, "r");

	if (file == NULL) {
		printf("Could not open file \n");
		return false;
	}

	float* lut_data = nullptr;
	int size = 0;

	// Iterate through lines
	while (true) {
		char line[128];
		fscanf(file, "%128[^\n]\n", line);



		if (strcmp(line, "#LUT size") == 0) {
			// Read LUT size
			fscanf(file, "%s %i\n", &line, &size);
			lut_data = new float[size * size * size * 3];
		}
		else if (strcmp(line, "#LUT data points") == 0) {

			// Read colors
			int row = 0;
			do {
				float r, g, b;
				fscanf(file, "%f %f %f\n", &r, &g, &b);
				lut_data[row * 3 + 0] = r;
				lut_data[row * 3 + 1] = g;
				lut_data[row * 3 + 2] = b;
				row++;
			} while (row < size * size * size);
			break;
		}
	}
	fclose(file);

	// Create texture
	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_3D, texture);

	// Load data to texture
	glTexImage3D(

		GL_TEXTURE_3D,
		0,
		GL_RGB,
		size, size, size,
		0,
		GL_RGB,
		GL_FLOAT,
		lut_data
	);

	// Set sampling parameters
	glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP);

	return texture;

}

/// @brief calculates mimap level for framebuffer textures
/// @param w width of the texture
/// @param h height of the texture
/// @return the number of mipmap levles
int Texture::getNumMipMapLevels2D(int w, int h)
{
	int levels = 1;
	while ((w | h) >> levels)
		levels += 1;
	return levels;
}

void Texture::stbiLoad(std::string texPath, stbiData* img)
{
	stbi_set_flip_vertically_on_load(true);

	img->data = stbi_load(texPath.c_str(), &img->w, &img->h, &img->comp, 3);
}

/// @brief adds a subfolder to a given path
/// @param texPath is the path to the root folder
/// @param texType is the name of the image file in the root folder
/// @return the path to the image file
std::string Texture::append(const char* texPath, char* texType)
{
	char c[100];
	strcpy(c, texPath);
	std::string result = strcat(c, texType);
	return result;
}