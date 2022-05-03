/*
* Copyright 2021 Vienna University of Technology.
* Institute of Computer Graphics and Algorithms.
* This file is part of the ECG Lab Framework and must not be redistributed.
*/


/*
 Main funtion of the game "Greed" by David Köppl and Nicolas Eder
 contains initialization, resource loading and render loop
*/

#include <sstream>
#include <thread>
#include "Camera.h"
#include "Renderer.h"
#include "FPSCounter.h"
#include "GLFWApp.h"
#include "Debugger.h"
#include "Level.h"
#include "Physics.h"
#include "PlayerController.h"
#include "LoadingScreen.h"
#include <irrKlang/irrKlang.h>
#include <optick/optick.h>

/* --------------------------------------------- */
// Global variables
/* --------------------------------------------- */

std::shared_ptr<global_state> state_;
keyboard_input_state keyboard_input_;
PerFrameData perframe_data_;
mouse_state mouse_state_;

camera_positioner_interface* camera_positioner_;
camera_positioner_first_person floating_positioner_(glm::vec3(0.0f, 1.85f, 70.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
camera_positioner_player player_camera_positioner_;
camera camera_(*camera_positioner_);

void registerInputCallbacks(glfw_app& app);

/* --------------------------------------------- */
// Main
/* --------------------------------------------- */

int main(int argc, char** argv)
{
	printf("Starting program...\n");
	OPTICK_THREAD("MainThread")
	OPTICK_START_CAPTURE()
	OPTICK_PUSH("init program")
	/* --------------------------------------------- */
	// Load settings.ini
	/* --------------------------------------------- */

	std::ifstream file("../../assets/submission1.fbx");
	// if this assertion fails, and you cloned this project from Github,
	// try setting your working directory of the debugger to "$(TargetDir)"
	assert(file.is_open());
	file.close();

	state_ = renderer::get_state();

	/* --------------------------------------------- */
	// Init framework
	/* --------------------------------------------- */

	// setup GLFW window
	printf("Initializing GLFW...\n");
	glfw_app glfw_app(state_);
	registerInputCallbacks(glfw_app);

	// load all OpenGL function pointers with GLEW
	printf("Initializing GLEW...\n");
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
		EXIT_WITH_ERROR("Failed to load GLEW\n");

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(debugger::debug_callback_default, 0);
	
	LoadingScreen loading_screen = LoadingScreen(&glfw_app, state_->width, state_->height);
	loading_screen.draw_progress();

	/* --------------------------------------------- */
	// Initialize scene and render loop
	/* --------------------------------------------- */

	printf("Initializing scene and render loop...\n");

	printf("Initializing audio...\n"); 
	irrklang::ISoundEngine* engine = irrklang::createIrrKlangDevice();
	irrklang::ISound* snd = engine->play2D("../../assets/media/EQ07 Prc Fantasy Perc 060.wav", true);
	
	loading_screen.draw_progress();
	printf("Loading level...\n");
	OPTICK_PUSH("load level")
	level level("../../assets/submission1.fbx", state_, perframe_data_);
	OPTICK_POP()

	loading_screen.draw_progress();
	printf("Initializing renderer...\n");
	OPTICK_PUSH("load renderer")
	renderer renderer(perframe_data_, *level.get_lights());
	OPTICK_POP()

	loading_screen.draw_progress();
	//Physics Initialization
	printf("Initializing physics...\n");
	OPTICK_PUSH("init physics")
	Physics physics;
	OPTICK_POP()

	// Integrate level meshes into physics world
	std::vector<physics_mesh> dynamicMeshes = level.get_dynamic();
	for (auto& dynamicMeshe : dynamicMeshes)
	{
		Physics::PhysicsObject obj = physics.createPhysicsObject(
			dynamicMeshe.node,
			dynamicMeshe.model_trs,
			dynamicMeshe.vtx_positions,
			Physics::ObjectMode::Dynamic
		);
		obj.modelGraphics->game_properties.is_collectable = true; // temporary solution
	}

	std::vector<physics_mesh> staticMeshes = level.get_rigid();
	for (auto& staticMeshe : staticMeshes)
		physics.createPhysicsObject(
			staticMeshe.node,
			staticMeshe.model_trs,
			staticMeshe.vtx_positions,
			Physics::ObjectMode::Static
		);

	// Setup camera
	camera_positioner_ = &player_camera_positioner_;
	camera_.set_positioner(camera_positioner_);
	player_camera_positioner_.set_position(glm::vec3(0, 10, 0));

	player_controller player(physics, player_camera_positioner_, glm::vec3(0, 20, 0));

	item_collection item_collection;

	glViewport(0, 0, state_->width, state_->height);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(2.0f);
	glEnable(GL_CULL_FACE);

	engine->stopAllSounds();
	snd = engine->play2D("../../assets/media/Wolum - Greed Collecting.mp3", true);

	double time_stamp = glfwGetTime();
	float delta_seconds = 0.0f;
	fps_counter fps_counter{};

	glfwSetInputMode(glfw_app.get_window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	OPTICK_POP()

	//---------------------------------- RENDER LOOP ----------------------------------//

	printf("Entering render loop...\n");
	while (!glfwWindowShouldClose(glfw_app.get_window()))
	{
		OPTICK_PUSH("render loop")
		OPTICK_PUSH("game logic")
		fps_counter.tick(delta_seconds);

		// fps counter
		delta_seconds = glfw_app.get_delta_seconds();
		std::string title = state_->window_title + " " + fps_counter.get_fps() + " fps";
		glfwSetWindowTitle(glfw_app.get_window(), title.c_str());

		// variable window size
		glViewport(0, 0, state_->width, state_->height);
		glfw_app.update_window();

		// player actions
		if (state_->using_debug_camera)
			floating_positioner_.set_movement_state(keyboard_input_);
		else {
			player.move(keyboard_input_, delta_seconds);
			state_->display_collect_item_hint = player.has_collectable_item_in_reach();
			player.try_collect_item(mouse_state_, keyboard_input_, item_collection);
		}
		OPTICK_POP()

		// calculate physics
		OPTICK_PUSH("physics simulation")
		physics.simulateOneStep(delta_seconds);
		OPTICK_POP()

		OPTICK_PUSH("draw routine")
		// update camera
		player.update_camera_positioner();
		camera_positioner_->update(delta_seconds, mouse_state_.pos, mouse_state_.pressed_left);

		// calculate and set per frame matrices
		const float ratio = static_cast<float>(state_->width) / static_cast<float>(state_->height);
		const glm::mat4 projection = glm::perspective(glm::radians(state_->fov), ratio, state_->znear, state_->zfar);
		const glm::mat4 view = camera_.get_view_matrix();
		perframe_data_.view_proj = projection * view;
		perframe_data_.view_pos = glm::vec4(camera_.get_position(), 1.0f);
		perframe_data_.view_inv = glm::inverse(view);
		perframe_data_.proj_inv = glm::inverse(projection);
		perframe_data_.delta_time.x = delta_seconds;
		perframe_data_.delta_time.y += delta_seconds;

		// simple game logic WIP
		state_->total_cash = item_collection.get_total_monetary_value();
		state_->collected_items = static_cast<int>(item_collection.size());
		if (perframe_data_.view_pos.y > 127.0f)
		{
			state_->won = true;
		}
		/*if (perframe_data_.view_pos.y - 1.8f < lava_position.y)
		{
			state_->lost = true;
		}*/

		// actual draw call
		renderer.draw(&level);
		if (state_->debug_draw_physics)
			physics.debugDraw();

		// swap buffers
		glfw_app.swap_buffers();
		renderer.swap_luminance();
		OPTICK_POP()
		OPTICK_POP()
	}


	/* --------------------------------------------- */
	// Destroy context and exit
	/* --------------------------------------------- */
	OPTICK_STOP_CAPTURE()
	OPTICK_SAVE_CAPTURE("profiler_dump")
	printf("Exiting program...\n");
	return EXIT_SUCCESS;
}

void registerInputCallbacks(glfw_app& app) {
	glfwSetKeyCallback(app.get_window(),
		[](GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			// Movement
			const bool press = action != GLFW_RELEASE;
			if (key == GLFW_KEY_W)
				keyboard_input_.pressing_w = press;
			if (key == GLFW_KEY_S)
				keyboard_input_.pressing_s = press;
			if (key == GLFW_KEY_A)
				keyboard_input_.pressing_a = press;
			if (key == GLFW_KEY_D)
				keyboard_input_.pressing_d = press;
			if (key == GLFW_KEY_1)
				keyboard_input_.pressing_1 = press;
			if (key == GLFW_KEY_2)
				keyboard_input_.pressing_2 = press;
			if (mods & GLFW_MOD_SHIFT)
				keyboard_input_.pressing_shift = press;
			if (key == GLFW_KEY_SPACE)
				keyboard_input_.pressing_space = press;

			// Window management, Debug, Effects
			if (key == GLFW_KEY_ESCAPE)
				glfwSetWindowShouldClose(window, GLFW_TRUE);
			if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
			{
				if (state_->fullscreen)
					printf("Fullscreen off\n");
				else
					printf("Fullscreen on\n");

				state_->fullscreen = !state_->fullscreen;
			}
			if (key == GLFW_KEY_F2 && action == GLFW_PRESS)
				state_->cull_debug = !state_->cull_debug;
			if (key == GLFW_KEY_F3 && action == GLFW_PRESS)
			{
				if (state_->bloom)
					printf("Bloom off\n");
				else
					printf("Bloom on\n");

				state_->bloom = !state_->bloom;
			}
			if (key == GLFW_KEY_F4 && action == GLFW_PRESS)
			{
				if (state_->debug_draw_physics)
					printf("Physics debugging off\n");
				else
					printf("Physics debugging on\n");

				state_->debug_draw_physics = !state_->debug_draw_physics;
			}
			if (key == GLFW_KEY_F5 && action == GLFW_PRESS)
			{
				if (perframe_data_.normal_map.x > 0.0f)
					printf("normal mapping off\n");
				else
					printf("normal mapping on\n");

				perframe_data_.normal_map.x *= -1.0f;
			}
			if (key == GLFW_KEY_F6 && action == GLFW_PRESS) {
				if (state_->using_debug_camera) {
					printf("Switch camera to player\n");
					camera_positioner_ = &player_camera_positioner_;
					state_->debug_draw_physics = false;
					glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
				}
				else {
					printf("Switch camera to debug camera\n");
					camera_positioner_ = &floating_positioner_;
					state_->debug_draw_physics = true;
					glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
				}
				state_->using_debug_camera = !state_->using_debug_camera;
				camera_.set_positioner(camera_positioner_);
			}
			if (key == GLFW_KEY_F7 && action == GLFW_PRESS)
			{
				if (state_->freeze_cull)
				{
					printf("resume frustum culling\n");
					state_->freeze_cull = false;
				}
				else {
					printf("freeze frustum culling\n");
					state_->freeze_cull = true;
				}
			}
			if (key == GLFW_KEY_F8 && action == GLFW_PRESS)
			{
				if (state_->cull)
				{
					printf("frustum culling off\n");
					state_->cull = false;
				}
				else {
					printf("frustum culling on\n");
					state_->cull = true;
				}
			}
			if (key == GLFW_KEY_F9 && action == GLFW_PRESS)
			{
				if (state_->ssao)
				{
					printf("SSAO off\n");
					state_->ssao = false;
				}
				else {
					printf("SSAO on\n");
					state_->ssao = true;
				}
			}
		});
	glfwSetMouseButtonCallback(app.get_window(),
		[](auto* window, int button, int action, int mods)
		{
			if (button == GLFW_MOUSE_BUTTON_LEFT)
				mouse_state_.pressed_left = action == GLFW_PRESS;

			if (button == GLFW_MOUSE_BUTTON_RIGHT)
				mouse_state_.pressed_right = action == GLFW_PRESS;

		});
	glfwSetCursorPosCallback(
		app.get_window(), [](auto* window, double x, double y) {
			int w, h;
			glfwGetFramebufferSize(window, &w, &h);
			mouse_state_.pos.x = static_cast<float>(x / w);
			mouse_state_.pos.y = static_cast<float>(y / h);
		}
	);
}