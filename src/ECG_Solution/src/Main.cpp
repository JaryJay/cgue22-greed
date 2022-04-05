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
#include <numbers>
#include "Camera.h"
#include "Renderer.h"
#include "FPSCounter.h"
#include "GLFWApp.h"
#include "Debugger.h"
#include "Level.h"
#include "BulletDebugDrawer.h"
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
const double PI = 3.141592653589793238463;

static btConvexHullShape* getHullShapeFromMesh(Mesh* mesh);
static btRigidBody makeRigidbody(btQuaternion rot, btVector3 pos, btCollisionShape* col, btScalar mass);
static glm::vec3 btToGlmVector(btVector3 input);

/* --------------------------------------------- */
// Main
/* --------------------------------------------- */

int main(int argc, char** argv)
{
	printf("Starting program...\n");

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
					printf("Fullscreen off");
					globalState.fullscreen_ = false;
				}
				else {
					printf("Fullscreen on");
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
					printf("Bloom off");
					globalState.bloom_ = false;
				}
				else {
					printf("Bloom on");
					globalState.bloom_ = true;
				}
			}
			if (key == GLFW_KEY_F4 && action == GLFW_PRESS)
			{
				if (globalState.debugDrawPhysics)
				{
					printf("Physics debugging off");
					globalState.debugDrawPhysics = false;
				}
				else {
					printf("Physics debugging on");
					globalState.debugDrawPhysics = true;
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
	printf("Initializing GLEW...");
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
		EXIT_WITH_ERROR("Failed to load GLEW");

	//part of the ECG magical framework
	printf("Initializing framework...\n");
	if (!initFramework())
		EXIT_WITH_ERROR("Failed to init framework");

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(Debugger::DebugCallbackDefault, 0);

	/* --------------------------------------------- */
	// Initialize scene and render loop
	/* --------------------------------------------- */

	printf("Initializing scene and render loop...");

	// load models and textures
	LevelInterface* level = new ModelTesterLevel();
	Renderer renderer(globalState, perframeData, level->getLights());

	//-------------------------WIP------------------------------------------//
	Material gold("assets/textures/coin");
	Material rock("assets/textures/rockground");
	Material wood("assets/textures/wood");

	Mesh coin1 = Mesh("assets/models/coin.obj", &wood);
	coin1.setMatrix(glm::vec3(1.0f, -1.0f, -5.0f), 10.0f, glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(1));
	Mesh coin2 = Mesh("assets/models/coin.obj", &rock);
	coin2.setMatrix(glm::vec3(0.0f, 0.0f, -7.0f), 90.0f, glm::vec3(-1.0f, .0f, 0.0f), glm::vec3(0.5));
	Mesh coin3 = Mesh("assets/models/coin.obj", &gold);
	coin3.setMatrix(glm::vec3(-1.0f, 1.0f, -5.0f), 45.0f, glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.5));
	Mesh groundPlane = Mesh("assets/models/plane.obj", &rock);
	groundPlane.setMatrix(glm::vec3(0.0f, -5.0f, 0.0f), 1.0f, glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(20.0f, 1.0f, 20.0f));

	std::vector <Mesh*> models;
	models.push_back(&coin1);
	models.push_back(&coin2);
	models.push_back(&coin3);
	models.push_back(&groundPlane);

	//Bullet Initialization
	printf("Initializing bullet physics...\n");
	btDbvtBroadphase* broadphase = new btDbvtBroadphase();
	btDefaultCollisionConfiguration* collision_configuration = new btDefaultCollisionConfiguration();
	btCollisionDispatcher* dispatcher = new btCollisionDispatcher(collision_configuration);
	btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver;
	btDiscreteDynamicsWorld* dynamics_world = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collision_configuration);
	dynamics_world->setGravity(btVector3(0, -10, 0));
	BulletDebugDrawer* bulletDebugDrawer = new BulletDebugDrawer();
	bulletDebugDrawer->setDebugMode(btIDebugDraw::DBG_DrawWireframe);
	dynamics_world->setDebugDrawer(bulletDebugDrawer);

	btCollisionShape* collider = getHullShapeFromMesh(&coin1);
	btRigidBody fallingCoin = makeRigidbody(btQuaternion(btVector3(1, 0, 0), 45), btVector3(0.0, 0.0, 0.0), collider, 1);

	btVector3* boxSize2 = new btVector3(20, 0.0, 20.0);
	btCollisionShape* collider2 = new btBoxShape(*boxSize2);
	btRigidBody staticPlane = makeRigidbody(btQuaternion(0.0, 0.0, 0.0), btVector3(0.0, -10.0, 0.0), collider2, 0);

	dynamics_world->addRigidBody(&fallingCoin);
	dynamics_world->addRigidBody(&staticPlane);
	//-------------------------/WIP------------------------------------------//

	glViewport(0, 0, globalState.width, globalState.height);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	double timeStamp = glfwGetTime();
	float deltaSeconds = 0.0f;
	FPSCounter fpsCounter = FPSCounter();

	glfwSetInputMode(GLFWapp.getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

	//---------------------------------- RENDER LOOP ----------------------------------//

	printf("Entering render loop...");
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

		// calculate physics
		dynamics_world->stepSimulation(deltaSeconds);

		glm::vec3 pos = btToGlmVector(fallingCoin.getCenterOfMassTransform().getOrigin());
		float deg = (float)(fallingCoin.getOrientation().getAngle() * 180 / PI);
		glm::vec3 axis = btToGlmVector(fallingCoin.getOrientation().getAxis());
		glm::vec3 scale = glm::vec3(0.5);
		models[0]->setMatrix(pos, deg, axis, scale);

		glm::vec3 pos2 = btToGlmVector(staticPlane.getCenterOfMassTransform().getOrigin());
		float deg2 = (float)(staticPlane.getOrientation().getAngle() * 180 / PI);
		glm::vec3 axis2 = btToGlmVector(staticPlane.getOrientation().getAxis());
		glm::vec3 scale2 = glm::vec3(20.0f, 1.0f, 20.0f);
		models[3]->setMatrix(pos2, deg2, axis2, scale2);

		// calculate and set per Frame matrices
		const float ratio = globalState.width / (float)globalState.height;
		const glm::mat4 projection = glm::perspective(glm::radians(globalState.fov), ratio, globalState.Znear, globalState.Zfar);
		const glm::mat4 view = camera.getViewMatrix();
		perframeData.ViewProj = projection * view;
		perframeData.ViewProjSkybox = projection * glm::mat4(glm::mat3(view)); // remove translation
		perframeData.viewPos = glm::vec4(camera.getPosition(), 1.0f);

		// actual draw call
		renderer.Draw(models);
		if (globalState.debugDrawPhysics) {
			dynamics_world->debugDrawWorld();
			bulletDebugDrawer->draw();
		}

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


	printf("Exiting programm...");

	return EXIT_SUCCESS;
}

static btConvexHullShape* getHullShapeFromMesh(Mesh* mesh) {
	btConvexHullShape* shape = new btConvexHullShape();
	btScalar* coordinates = (*mesh).getVerticeCoordinates();
	int verticeAmount = (*mesh).getVerticeAmount();
	for (int i = 0; i < verticeAmount; i++)
	{
		btScalar x = coordinates[i * 3];
		btScalar y = coordinates[i * 3 + 1];
		btScalar z = coordinates[i * 3 + 2];
		shape->addPoint(btVector3(x, y, z));
	}

	return shape;
}

static btRigidBody makeRigidbody(btQuaternion rot, btVector3 pos, btCollisionShape* col, btScalar mass) {

	btTransform* startTransform = new btTransform(rot, pos);
	btMotionState* motionSate = new btDefaultMotionState(*startTransform);
	btVector3 inertia;
	col->calculateLocalInertia(mass, inertia);
	return btRigidBody(mass, motionSate, col, inertia);
}

static glm::vec3 btToGlmVector(btVector3 input) {
	return glm::vec3((float)input.getX(), (float)input.getY(), (float)input.getZ());
}