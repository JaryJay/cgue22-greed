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
#include "Debugger.h"
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

/* --------------------------------------------- */
// Main
/* --------------------------------------------- */

int main(int argc, char** argv)
{
	std::cout << "starting program..." << std::endl;
	std::cout << std::endl;

	/*//Bullet Initialization
	btDbvtBroadphase* broadphase = new btDbvtBroadphase();
	btDefaultCollisionConfiguration* collision_configuration = new btDefaultCollisionConfiguration();
	btCollisionDispatcher* dispatcher = new btCollisionDispatcher(collision_configuration);
	btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver;
	btDiscreteDynamicsWorld* dynamics_world = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collision_configuration);
	dynamics_world->setGravity(btVector3(0, -10, 0));*/

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
	glDebugMessageCallback(Debugger::DebugCallbackDefault, 0);

	/* --------------------------------------------- */
	// Initialize scene and render loop
	/* --------------------------------------------- */

	std::cout << "initialize scene and render loop..." << std::endl;
	Renderer renderer(globalState,perframeData);

	std::cout << "initialize models and textuers..." << std::endl;
	Texture brickDiff("assets/textures/brick03-diff.jpeg");
	Texture brickSpec("assets/textures/brick03-spec.jpeg");
	Cubemap brickCube("assets/textures/cubemap");

	Material brick(&brickDiff,&brickSpec,&brickCube,
		glm::vec4(0.5f, 0.5f, 1.0f, 1.0f), 1.0f);

	Material sky(&brickDiff, &brickSpec, &brickCube,
		glm::vec4(0.0f, 0.0f, 0.0f, 0.0f), 1.0f);

	Mesh coin("assets/models/coin.obj", &brick);
	coin.translate(glm::vec3(0.0f, 0.0f, -5.0f));

	Mesh skybox = skybox.Skybox(400.0f, &sky);

	Mesh box = box.Cube(1.5f, 1.5f, 1.5f, &brick);
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

