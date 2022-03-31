/*
* Copyright 2021 Vienna University of Technology.
* Institute of Computer Graphics and Algorithms.
* This file is part of the ECG Lab Framework and must not be redistributed.
*/


/*
 Main funtion of the game "Greed" by David K�ppl and Nicolas Eder
 contains initialization, resource loading and render loop
*/

#pragma once
#include <sstream>
#include "Camera.h"
#include "Renderer.h"
#include "FPSCounter.h"
#include "GLFWApp.h"
#include "Debugger.h"
#include "Level.h"
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <assimp/version.h>
#include <bullet/btBulletCollisionCommon.h>
#include <bullet/btBulletDynamicsCommon.h>

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

static void print(string s);

/* --------------------------------------------- */
// Main
/* --------------------------------------------- */

int main(int argc, char** argv)
{
	print("starting program...\n");

	/* --------------------------------------------- */
	// Load settings.ini
	/* --------------------------------------------- */

	globalState = Renderer::loadSettings(globalState);

	/* --------------------------------------------- */
	// Init framework
	/* --------------------------------------------- */

	//setup GLFW window
	GLFWApp GLFWapp(globalState);

	// register input callbacks to window
	glfwSetKeyCallback(GLFWapp.getWindow(),
		[](GLFWwindow* window,
			int key, int scancode, int action, int mods)
		{
			// Movement
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

			// Debug & Effects
			if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
			{
				if (globalState.fullscreen_)
				{
					print("fullscreen off");
					globalState.fullscreen_ = false;
				}
				else {
					print("fullscreen on");
					globalState.fullscreen_ = true;
				}
			}
			if (key == GLFW_KEY_F2 && action == GLFW_PRESS) //TODO
			{
				if (globalState.focus_)
				{
					globalState.request_focus_ = true;
				}
				else
				{

					globalState.request_unfocus_ = true;
				}
			}
			if (key == GLFW_KEY_F3 && action == GLFW_PRESS)
			{
				if (globalState.bloom_)
				{
					print("bloom off");
					globalState.bloom_ = false;
				}
				else {
					print("bloom on");
					globalState.bloom_ = true;
				}
			}
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
			//glfwSetCursorPos(window, 0, 0); // cursor disabled kind of fix
		}
	);

	// load all OpenGL function pointers with GLEW
	print("initializing GLEW...");
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
		EXIT_WITH_ERROR("failed to load GLEW");

	//part of the ECG magical framework
	print("initialize framework...\n");
	if (!initFramework())
		EXIT_WITH_ERROR("failed to init framework");

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(Debugger::DebugCallbackDefault, 0);

	/* --------------------------------------------- */
	// Initialize scene and render loop
	/* --------------------------------------------- */

	print("initialize scene and render loop...");

	// load models and textures
	print("loading level...");
	Level level("assets/Bistro_v5_2/BistroInterior.fbx"); // https://developer.nvidia.com/orca/amazon-lumberyard-bistro
	print("intializing renderer...");
	Renderer renderer(globalState, perframeData, level.getLights());

	//-------------------------WIP------------------------------------------//
	print("loading material...");
	Material gold("assets/textures/coin");
	Material rock("assets/textures/rockground");
	Material wood("assets/textures/wood");

	print("loading models...");
	Mesh coin1 = Mesh("assets/models/coin.obj", &wood);
	coin1.setMatrix(glm::vec3(1.0f, -1.0f, -5.0f), 10.0f, glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(0.5));
	Mesh coin2 = Mesh("assets/models/coin.obj", &rock);
	coin2.setMatrix(glm::vec3(0.0f, 0.0f, -7.0f), 90.0f, glm::vec3(-1.0f, .0f, 0.0f), glm::vec3(0.5));
	Mesh coin3 = Mesh("assets/models/coin.obj", &gold);
	coin3.setMatrix(glm::vec3(-1.0f, 1.0f, -5.0f), 45.0f, glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.5));

	std::vector <Mesh*> models;
	models.push_back(&coin1);
	models.push_back(&coin2);
	models.push_back(&coin3);
	//-------------------------/WIP------------------------------------------//

	glViewport(0, 0, globalState.width, globalState.height);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	double timeStamp = glfwGetTime();
	float deltaSeconds = 0.0f;
	FPSCounter fpsCounter = FPSCounter(); 
	
	glfwSetInputMode(GLFWapp.getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

	//---------------------------------- RENDER LOOP ----------------------------------//

	print("enter render loop...");
	while (!glfwWindowShouldClose(GLFWapp.getWindow()))
	{
		fpsCounter.tick(deltaSeconds);

		//positioner.update(deltaSeconds, mouseState.pos, globalState.focus_);
		positioner.update(deltaSeconds, mouseState.pos, mouseState.pressedLeft);

		// fps counter
		const double newTimeStamp = glfwGetTime();
		deltaSeconds = static_cast<float>(newTimeStamp - timeStamp);
		timeStamp = newTimeStamp;
		std::string title = globalState.window_title + " " + fpsCounter.getFPS() + " fps";
		glfwSetWindowTitle(GLFWapp.getWindow(), title.c_str());

		// variable window size
		glViewport(0, 0, globalState.width, globalState.height); 
		GLFWapp.updateWindow();

		// calculate and set per Frame matrices
		const float ratio = globalState.width / (float)globalState.height;
		const glm::mat4 projection = glm::perspective(glm::radians(globalState.fov), ratio, globalState.Znear, globalState.Zfar);
		const glm::mat4 view = camera.getViewMatrix();
		perframeData.ViewProj = projection * view;
		perframeData.ViewProjSkybox = projection * glm::mat4(glm::mat3(view)); // remove translation
		perframeData.viewPos = glm::vec4(camera.getPosition(),1.0f);

		// actual draw call
		//renderer.Draw(models);
		renderer.Draw(&level);

		// swap buffers
		GLFWapp.swapBuffers();
		renderer.swapLuminance();
	}

	/* --------------------------------------------- */
	// Destroy framework
	/* --------------------------------------------- */

	destroyFramework();


	/* --------------------------------------------- */
	// Destroy context and exit
	/* --------------------------------------------- */


	print("exit programm...");

	return EXIT_SUCCESS;
}

static void print(string s) {
	std::cout << s << std::endl;
}