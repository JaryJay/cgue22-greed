/*
* Copyright 2021 Vienna University of Technology.
* Institute of Computer Graphics and Algorithms.
* This file is part of the ECG Lab Framework and must not be redistributed.
*/


#pragma once
#include <sstream>
#include "Camera.h"
#include "Renderer.h"
#include "FPSCounter.h"
#include "GLFWApp.h"


/* --------------------------------------------- */
// Prototypes
/* --------------------------------------------- */

static void APIENTRY DebugCallbackDefault(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const GLvoid* userParam);
static std::string FormatDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, const char* msg);

/* --------------------------------------------- */
// Global variables
/* --------------------------------------------- */

GlobalState globalState;

PerFrameData perframeData;

struct MouseState
{
	glm::vec2 pos = glm::vec2(0.0f);
	bool pressedLeft = false;
	bool pressedRight = false;
} mouseState;


CameraPositioner_FirstPerson positioner(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
Camera camera(positioner);

/* --------------------------------------------- */
// Main
/* --------------------------------------------- */

int main(int argc, char** argv)
{
	std::cout << "starting program..." << std::endl;
	std::cout << std::endl;

	/* --------------------------------------------- */
	// Load settings.ini
	/* --------------------------------------------- */

	globalState = Renderer::loadSettings(globalState);

	/* --------------------------------------------- */
	// Init framework
	/* --------------------------------------------- */

	//load GLFW
	GLFWApp GLFWapp(globalState);

	// register input callbacks to window
	glfwSetKeyCallback(GLFWapp.getWindow(),
		[](GLFWwindow* window,
			int key, int scancode, int action, int mods)
		{
			const bool press = action != GLFW_RELEASE;
			if (key == GLFW_KEY_ESCAPE)
				glfwSetWindowShouldClose(window, GLFW_TRUE);
			if (key == GLFW_KEY_W)
				positioner.movement_.forward_ = press;
			if (key == GLFW_KEY_S)
				positioner.movement_.backward_ = press;
			if (key == GLFW_KEY_A)
				positioner.movement_.left_ = press; 
			if (key == GLFW_KEY_D)
				positioner.movement_.right_ = press;
			if (key == GLFW_KEY_1)
				positioner.movement_.up_ = press;
			if (key == GLFW_KEY_2)
				positioner.movement_.down_ = press;
			if (mods & GLFW_MOD_SHIFT)
				positioner.movement_.fastSpeed_ = press;
			if (key == GLFW_KEY_SPACE)
				positioner.setUpVector(glm::vec3(0.0f, 1.0f, 0.0f));
		});
	glfwSetMouseButtonCallback(GLFWapp.getWindow(),
		[](auto* window, int button, int action, int mods)
		{
			if (button == GLFW_MOUSE_BUTTON_LEFT)
				mouseState.pressedLeft = action == GLFW_PRESS;

			if (button == GLFW_MOUSE_BUTTON_RIGHT)
				mouseState.pressedRight = action == GLFW_PRESS;

		});
	glfwSetCursorPosCallback(
		GLFWapp.getWindow(), [](auto* window, double x, double y) {
			int w, h;
			glfwGetFramebufferSize(window, &w, &h);
			mouseState.pos.x = static_cast<float>(x / w);
			mouseState.pos.y = static_cast<float>(y / h);
		}
	);

	// load all OpenGL function pointers with GLEW
	std::cout << "initializing GLEW..." << std::endl;
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK)
	{
		EXIT_WITH_ERROR("failed to load GLEW");
	}

	//do not delete this
	std::cout << "initialize framework..." << std::endl;
	if (!initFramework())
	{
		EXIT_WITH_ERROR("failed to init framework");
	}
	std::cout << std::endl;

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(DebugCallbackDefault, 0);

	/* --------------------------------------------- */
	// Initialize scene and render loop
	/* --------------------------------------------- */

	std::cout << "initialize scene and render loop..." << std::endl;
	Renderer renderer(globalState,perframeData);

	std::cout << "initialize models and textuers..." << std::endl;
	Texture goldAlbedo("assets/textures/Coin/albedo.jpg");
	Texture goldNormal("assets/textures/Coin/normal.jpg");
	Texture goldMetal("assets/textures/Coin/metal.jpg");
	Texture goldRough("assets/textures/Coin/rough.jpg");
	Texture goldAO("assets/textures/Coin/ao.jpg");
	Cubemap brickCube("assets/textures/cubemap");

	Material gold(&goldAlbedo,&goldNormal,&goldMetal,&goldRough,&goldAO,&brickCube);

	Mesh coin("assets/models/coin.obj", &gold);
	coin.translate(glm::vec3(0.0f, 0.0f, -5.0f));

	Mesh skybox = skybox.Skybox(400.0f, &gold);

	Mesh box = box.Cube(1.5f, 1.5f, 1.5f, &gold);
	box.translate(glm::vec3(0.0f, 0.0f, -5.0f));
	std::vector <Mesh*> models;
	models.push_back(&coin);

	// Use Depth Buffer
	std::cout << "enable depth buffer..." << std::endl;
	glEnable(GL_DEPTH_TEST);

	double timeStamp = glfwGetTime();
	float deltaSeconds = 0.0f;
	FPSCounter fpsCounter = FPSCounter();

	// locks mouse to window
	//glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	//---------------------------------- RENDER LOOP ----------------------------------//

	std::cout << "enter render loop..." << std::endl << std::endl;
	while (!glfwWindowShouldClose(GLFWapp.getWindow()))
	{
		positioner.update(deltaSeconds, mouseState.pos, mouseState.pressedLeft);

		// fps counter
		const double newTimeStamp = glfwGetTime();
		deltaSeconds = static_cast<float>(newTimeStamp - timeStamp);
		timeStamp = newTimeStamp;
		fpsCounter.tick(deltaSeconds);
		std::string title = globalState.window_title + " " + fpsCounter.getFPS() + " fps";
		glfwSetWindowTitle(GLFWapp.getWindow(), title.c_str());

		// prepare depth buffer
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f); //RGBA

		glViewport(0, 0, globalState.width, globalState.height);

		glfwGetFramebufferSize(GLFWapp.getWindow(), &globalState.width, &globalState.height);
		const float ratio = globalState.width / (float)globalState.height;
		const glm::mat4 projection = glm::perspective(glm::radians(globalState.fov), ratio, globalState.Znear, globalState.Zfar);
		const glm::mat4 view = camera.getViewMatrix();
		perframeData.ViewProj = projection * view;
		perframeData.ViewProjSkybox = projection * glm::mat4(glm::mat3(view)); // remove translation
		perframeData.viewPos = glm::vec4(camera.getPosition(),1.0f);

		renderer.Draw(models, skybox);

		// swap back and front buffers
		GLFWapp.swapBuffers();
	}

	/* --------------------------------------------- */
	// Destroy framework
	/* --------------------------------------------- */

	destroyFramework();
	glfwTerminate();


	/* --------------------------------------------- */
	// Destroy context and exit
	/* --------------------------------------------- */


	std::cout << "exit programm..." << std::endl;

	return EXIT_SUCCESS;
}


// callbacks
static void APIENTRY DebugCallbackDefault(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const GLvoid* userParam) {
	if (id == 131185 || id == 131218) return; // ignore performance warnings from nvidia
	std::string error = FormatDebugOutput(source, type, id, severity, message);
	std::cout << error << std::endl;
}

static std::string FormatDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, const char* msg) {
	std::stringstream stringStream;
	std::string sourceString;
	std::string typeString;
	std::string severityString;

	// The AMD variant of this extension provides a less detailed classification of the error,
	// which is why some arguments might be "Unknown".
	switch (source) {
	case GL_DEBUG_CATEGORY_API_ERROR_AMD:
	case GL_DEBUG_SOURCE_API: {
		sourceString = "API";
		break;
	}
	case GL_DEBUG_CATEGORY_APPLICATION_AMD:
	case GL_DEBUG_SOURCE_APPLICATION: {
		sourceString = "Application";
		break;
	}
	case GL_DEBUG_CATEGORY_WINDOW_SYSTEM_AMD:
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM: {
		sourceString = "Window System";
		break;
	}
	case GL_DEBUG_CATEGORY_SHADER_COMPILER_AMD:
	case GL_DEBUG_SOURCE_SHADER_COMPILER: {
		sourceString = "Shader Compiler";
		break;
	}
	case GL_DEBUG_SOURCE_THIRD_PARTY: {
		sourceString = "Third Party";
		break;
	}
	case GL_DEBUG_CATEGORY_OTHER_AMD:
	case GL_DEBUG_SOURCE_OTHER: {
		sourceString = "Other";
		break;
	}
	default: {
		sourceString = "Unknown";
		break;
	}
	}

	switch (type) {
	case GL_DEBUG_TYPE_ERROR: {
		typeString = "Error";
		break;
	}
	case GL_DEBUG_CATEGORY_DEPRECATION_AMD:
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: {
		typeString = "Deprecated Behavior";
		break;
	}
	case GL_DEBUG_CATEGORY_UNDEFINED_BEHAVIOR_AMD:
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: {
		typeString = "Undefined Behavior";
		break;
	}
	case GL_DEBUG_TYPE_PORTABILITY_ARB: {
		typeString = "Portability";
		break;
	}
	case GL_DEBUG_CATEGORY_PERFORMANCE_AMD:
	case GL_DEBUG_TYPE_PERFORMANCE: {
		typeString = "Performance";
		break;
	}
	case GL_DEBUG_CATEGORY_OTHER_AMD:
	case GL_DEBUG_TYPE_OTHER: {
		typeString = "Other";
		break;
	}
	default: {
		typeString = "Unknown";
		break;
	}
	}

	switch (severity) {
	case GL_DEBUG_SEVERITY_HIGH: {
		severityString = "High";
		break;
	}
	case GL_DEBUG_SEVERITY_MEDIUM: {
		severityString = "Medium";
		break;
	}
	case GL_DEBUG_SEVERITY_LOW: {
		severityString = "Low";
		break;
	}
	default: {
		severityString = "Unknown";
		break;
	}
	}

	stringStream << "OpenGL Error: " << msg;
	stringStream << " [Source = " << sourceString;
	stringStream << ", Type = " << typeString;
	stringStream << ", Severity = " << severityString;
	stringStream << ", ID = " << id << "]";

	return stringStream.str();
}

