#include "Renderer.h"
#include <optick/optick.h>

std::shared_ptr<global_state> renderer::state = std::make_shared<global_state>(load_settings());
std::shared_ptr<global_state> renderer::get_state() { return state; }

renderer::renderer(PerFrameData& perframe_data, light_sources& sources)
: perframe_data_(&perframe_data), lights_(sources)
{
	assert(renderer::state != nullptr);

	OPTICK_PUSH("build shaders")
	build_shader_programs();
	OPTICK_POP()
	OPTICK_PUSH("prepare renderer")
	set_render_settings();
	fill_buffers(); 
	prepare_framebuffers();

	std::cout << "load Enviroment Map.." << std::endl;
	ibl_.load_hdr("../../assets/textures/cubemap/cellar.hdr");
	std::cout << "load Skybox.." << std::endl;
	sky_tex_.load_hdr("../../assets/textures/cubemap/beach.hdr");
	const GLuint textures[] = {ibl_.get_environment(), ibl_.get_pre_filter_id(), ibl_.get_bdrf_lut_id(), sky_tex_.get_environment() };
	glBindTextures(8, 4, textures);

	lut_3d_ = Texture::load_3dlut("../../assets/textures/look32.CUBE");
	glBindTextureUnit(13, lut_3d_);

	font_renderer_.init("../../assets/fonts/Quasimoda/Quasimoda-Regular.otf", state->width, state->height);
	lava_sim_.init(glm::ivec3(lights_.directional.size(), lights_.point.size(), 0));
	OPTICK_POP()
}

void renderer::fill_buffers() const
{
	// create Uniform Buffer Objects from light source struct vectors
	directional_lights_.reserve_memory(1, lights_.directional.size() * sizeof(directional_light), lights_.directional.data());
	positional_lights_.reserve_memory(2, lights_.point.size() * sizeof(positional_light), lights_.point.data());
	perframe_buffer_.reserve_memory(0, sizeof(PerFrameData), perframe_data_);
}

void renderer::set_render_settings() const
{
	perframe_data_->bloom = glm::vec4(
		state->exposure,
		state->max_white,
		state->bloom_strength,
		state->adaptation_speed);

	perframe_data_->normal_map.x = 1.0f;

	perframe_data_->ssao1 = glm::vec4(
		state->scale,
		state->bias,
		state->znear,
		state->zfar);
	perframe_data_->ssao2 = glm::vec4(
		state->radius,
		state->att_scale,
		state->dist_scale,
		1.0f);

	perframe_data_->delta_time = glm::vec4(0);
}

void renderer::build_shader_programs()
{
	Shader pbr_vert("../../assets/shaders/PBR/pbr.vert");
	Shader pbr_frag("../../assets/shaders/PBR/pbr.frag", glm::ivec3(lights_.directional.size(), lights_.point.size(), 0));
	pbr_shader_.build_from(pbr_vert, pbr_frag);
	pbr_shader_.use();

	Shader skybox_vert("../../assets/shaders/skybox/skybox.vert");
	Shader skybox_frag("../../assets/shaders/skybox/skybox.frag");
	skybox_shader_.build_from(skybox_vert, skybox_frag);
	skybox_shader_.use();

	Shader full_screen_triangle_vert("../../assets/shaders/fullScreenTriangle.vert");

	Shader bright_pass_frag("../../assets/shaders/Bloom/BrightPass.frag");
	bright_pass_.build_from(full_screen_triangle_vert, bright_pass_frag);

	Shader combine_hdr_frag("../../assets/shaders/Bloom/CombineHDR.frag");
	combine_hdr_.build_from(full_screen_triangle_vert, combine_hdr_frag);

	Shader blur_x_frag("../../assets/shaders/Bloom/BlurX.frag");
	Shader blur_y_frag("../../assets/shaders/Bloom/BlurY.frag");
	blur_x_.build_from(full_screen_triangle_vert, blur_x_frag);
	blur_y_.build_from(full_screen_triangle_vert, blur_y_frag);

	Shader luminance_frag("../../assets/shaders/Bloom/toLuminance.frag");
	to_luminance_.build_from(full_screen_triangle_vert, luminance_frag);

	Shader light_adapt_comp("../../assets/shaders/Bloom/lightAdaption.comp");
	light_adapt_.build_from(light_adapt_comp);

	Shader ssao_frag("../../assets/shaders/SSAO/SSAO.frag");
	Shader combine_ssao_frag("../../assets/shaders/SSAO/combineSSAO.frag");
	ssao_.build_from(full_screen_triangle_vert, ssao_frag);
	combine_ssao_.build_from(full_screen_triangle_vert, combine_ssao_frag);

	Shader render_img_vert("../../assets/shaders/fullScreenTriangle.vert");
	Shader render_img_frag("../../assets/shaders/HUD/fullScreenImage.frag");
	Shader render_col_frag("../../assets/shaders/HUD/fullScreenColor.frag");
	render_image_.build_from(render_img_vert, render_img_frag);
	render_color_.build_from(render_img_vert, render_col_frag);

	Shader depth_vert("../../assets/shaders/lightFX/depthMap.vert");
	Shader depth_frag("../../assets/shaders/lightFX/depthMap.frag");
	depth_map_.build_from(depth_vert, depth_frag);

	Shader volight_frag("../../assets/shaders/lightFX/VolumetricLight.frag", glm::ivec3(lights_.directional.size(), lights_.point.size(), 0));
	volumetric_light_.build_from(full_screen_triangle_vert, volight_frag);

	Shader down_vl_vert("../../assets/shaders/lightFX/downVL.vert");
	Shader down_vl_frag("../../assets/shaders/lightFX/downVL.frag");
	downsample_vl_.build_from(down_vl_vert, down_vl_frag);

	Shader up_vl_vert("../../assets/shaders/lightFX/upVL.vert");
	Shader up_vl_frag("../../assets/shaders/lightFX/upVL.frag");
	upsample_vl_.build_from(up_vl_vert, up_vl_frag);

	pbr_shader_.use();
	pbr_shader_.set_int("numDir", lights_.directional.size());
	pbr_shader_.set_int("numPos", lights_.point.size());
}

void renderer::prepare_framebuffers() {

	glGenTextures(1, &luminance1x1_);
	glTextureView(luminance1x1_, GL_TEXTURE_2D, luminance_.get_texture_color().get_handle(), GL_RGBA16F, 6, 1, 0, 1);

	const glm::vec4 startingLuminance(glm::vec3(0.0f), 1.0f);
	glTextureSubImage2D(luminance0_.get_handle(), 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, &startingLuminance[0]);

	pattern_ = Texture::load_texture("../../assets/shaders/SSAO/pattern.bmp");

	hud_ = Texture::load_texture_transparent("../../assets/textures/loading/alpha HUD.png");
}

void renderer::draw(level* level)
{
	
	glClearNamedFramebufferfv(framebuffer1_.get_handle(), GL_COLOR, 0, &(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)[0]));
	glClearNamedFramebufferfi(framebuffer1_.get_handle(), GL_DEPTH_STENCIL, 0, 1.0f, 0);

	const glm::vec3 dir = glm::normalize(lights_.directional[0].direction);
	const glm::mat4 light_view = glm_look_at(glm::vec3(0, 0, 0), -dir, glm::vec3(0, 0, 1));
	const glm::mat4 light_proj = level->get_tight_scene_frustum(light_view);
	perframe_data_->light_view_proj = light_proj * light_view;

	// TODO
	lights_.point[0].position = perframe_data_->view_pos;
	positional_lights_.update(lights_.point.size() * sizeof(positional_light), lights_.point.data());

	perframe_buffer_.update(sizeof(PerFrameData), perframe_data_);

	glEnable(GL_DEPTH_TEST);

	// 1 - depth mapping
#if 1 // under construction
	OPTICK_PUSH("depth pass")
	depth_map_fb_.bind();
		glClearNamedFramebufferfi(depth_map_fb_.get_handle(), GL_DEPTH_STENCIL, 0, 1.0f, 0);
		depth_map_.use();
		level->draw_scene_shadow_map();
	depth_map_fb_.unbind();
	glBindTextureUnit(12, depth_map_fb_.get_texture_depth().get_handle());
	OPTICK_POP()
#endif // constructions ends

	// 2 - render scene to framebuffer
	OPTICK_PUSH("scene pass")
	framebuffer1_.bind();

		// 2.1 - draw skybox (background)    
		skybox_shader_.use();
		glDepthMask(false);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glDepthMask(true);

		// 2.2 - draw scene
		pbr_shader_.use();
		level->draw_scene();

		// 2.3 - draw lava
		lava_sim_.update(perframe_data_->delta_time.x);
		lava_sim_.simulation_step();
		lava_sim_.draw();

	framebuffer1_.unbind(); 
	glGenerateTextureMipmap(framebuffer1_.get_texture_color().get_handle());
	glTextureParameteri(framebuffer1_.get_texture_color().get_handle(), GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

	glDisable(GL_DEPTH_TEST);
	OPTICK_POP()

	// Volumetric Light
	// https://github.com/metzzo/ezg17-transition
	if (false)
	{
		/*
		// down sample scene using depth aware downsampling
		downsampleVL.Use();
		depthHalfRes.bind();
		glClearNamedFramebufferfi(depthHalfRes.getHandle(), GL_DEPTH_STENCIL, 0, 1.0f, 0);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		depthHalfRes.unbind();
		glBindTextureUnit(12, depthHalfRes.getTextureDepth().getHandle());
		*/
		// calculate volumetric lighting
		volumetric_light_.use();
		blur0_.bind();
		glDrawArrays(GL_TRIANGLES, 0, 3);
		blur0_.unbind();
		glBindTextureUnit(12, blur0_.get_texture_color().get_handle());


		/*
		// blur volumetric lighting vertically
		// Blur X
		blur1.bind();
		BlurX.Use();
		glBindTextureUnit(12, blur0.getTextureColor().getHandle());
		glDrawArrays(GL_TRIANGLES, 0, 3);
		blur1.unbind();

		// blur volumetric lighting horizontally
		// Blur Y
		blur0.bind();
		BlurY.Use();
		glBindTextureUnit(12, blur1.getTextureColor().getHandle());
		glDrawArrays(GL_TRIANGLES, 0, 3);
		blur0.unbind();
		glBindTextureUnit(12, blur0.getTextureColor().getHandle());
		/*
		// get volumetric lighting to full resolution using depth aware upsampling
		upsampleVL.Use();
		glBindTextureUnit(9, framebuffer1.getTextureColor().getHandle());
		glBindTextureUnit(12, blur0.getTextureColor().getHandle());
		framebuffer3.bind();
		glDrawArrays(GL_TRIANGLES, 0, 3);
		framebuffer3.unbind();
		glBindTextureUnit(12, framebuffer3.getTextureColor().getHandle());
		*/
	}

	
	// 3 - Apply SSAO
	OPTICK_PUSH("SSAO pass")
	if (state->ssao)
	{
		//3.1 - render scene with ssao pattern
		glClearNamedFramebufferfv(ssao_fb_.get_handle(), GL_COLOR, 0, &(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)[0]));
		ssao_fb_.bind();
			ssao_.use();
			glBindTextureUnit(16, framebuffer1_.get_texture_depth().get_handle());
			glBindTextureUnit(17, pattern_);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		ssao_fb_.unbind();

		// 3.2 - blur SSAO image
		// Blur X
		blur_.bind();
			blur_x_.use();
			glBindTextureUnit(16, ssao_fb_.get_texture_color().get_handle());
			glDrawArrays(GL_TRIANGLES, 0, 3);
		blur_.unbind();
		// Blur Y
		ssao_fb_.bind();
			blur_y_.use();
			glBindTextureUnit(16, blur_.get_texture_color().get_handle());
			glDrawArrays(GL_TRIANGLES, 0, 3);
		ssao_fb_.unbind();

		glClearNamedFramebufferfv(framebuffer2_.get_handle(), GL_COLOR, 0, &(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)[0]));

		// 3.3 - combine SSAO with rendered scene
		glViewport(0, 0, state->width, state->height);

		framebuffer2_.bind();
			combine_ssao_.use();
			glBindTextureUnit(16, framebuffer1_.get_texture_color().get_handle());
			glBindTextureUnit(17, ssao_fb_.get_texture_color().get_handle());
			glDrawArrays(GL_TRIANGLES, 0, 3);
		framebuffer2_.unbind();
	}
	else
	{
		glBlitNamedFramebuffer(framebuffer1_.get_handle(), framebuffer2_.get_handle(), 0, 0, state->width, state->height,
			0, 0, state->width, state->height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}
	OPTICK_POP()

	// 4 - Apply Bloom
	OPTICK_PUSH("Bloom pass")
	if (state->bloom)
	{

		// 4.1 - downscale for addiational blur and convert framebuffer to luminance
		luminance_.bind();
			to_luminance_.use();
			glBindTextureUnit(16, framebuffer2_.get_texture_color().get_handle());
			glDrawArrays(GL_TRIANGLES, 0, 3);
		luminance_.unbind();
		glGenerateTextureMipmap(luminance_.get_texture_color().get_handle());

		// 4.2 - compute light adaption (OpenGL memory model requires these memory barriers: https://www.khronos.org/opengl/wiki/Memory_Model )
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		light_adapt_.use();
		glBindImageTexture(0, luminances_[0]->get_handle(), 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, luminance1x1_, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, luminances_[1]->get_handle(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute(1, 1, 1);
		glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

		// 4.3 - filter bright spots from framebuffer
		bright_pass_fb_.bind();
			bright_pass_.use();
			glBindTextureUnit(16, framebuffer2_.get_texture_color().get_handle());
			glDrawArrays(GL_TRIANGLES, 0, 3);
		bright_pass_fb_.unbind();
		glBlitNamedFramebuffer(bright_pass_fb_.get_handle(), bloom1_.get_handle(), 0, 0, 256, 256, 0, 0, 256, 256, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		// 4.4 - blur bright spots using ping pong buffers and a seperate blur in x and y direction
		for (int i = 0; i < 4; i++)
		{
			// blur x
			bloom0_.bind();
				blur_x_.use();
				glBindTextureUnit(16, bloom1_.get_texture_color().get_handle());
				glDrawArrays(GL_TRIANGLES, 0, 3);
			bloom0_.unbind();
			// blur y
			bloom1_.bind();
				blur_y_.use();
				glBindTextureUnit(16, bloom0_.get_texture_color().get_handle());
				glDrawArrays(GL_TRIANGLES, 0, 3);
			bloom1_.unbind();
		}

		// 4.5 - combine framebuffer with blurred image 
		glViewport(0, 0, state->width, state->height);

		combine_hdr_.use();
		glBindTextureUnit(16, framebuffer2_.get_texture_color().get_handle());
		glBindTextureUnit(17, luminances_[1]->get_handle());
		glBindTextureUnit(18, bloom1_.get_texture_color().get_handle());
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}
	else
	{
		glBlitNamedFramebuffer(framebuffer2_.get_handle(), 0, 0, 0, state->width, state->height, 0, 0, 
			state->width, state->height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}
	OPTICK_POP()
	
	// 5 - render HUD
	OPTICK_PUSH("HUD pass")
	render_image_.use();
	glEnable(GL_BLEND);
	glBindTextureUnit(16, hud_);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	font_renderer_.print("CLOSED BETA FOOTAGE", state->width * 0.8f, state->height * 0.08f, .5f, glm::vec3(.7f, .7f, .7f));
	font_renderer_.print("all content is subject to change", state->width * 0.78f, state->height * 0.05f, .5f, glm::vec3(.5f, .5f, .5f));

	render_color_.use();
	render_color_.set_vec4("color", glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));
	glViewport(state->width * 0.04, state->height * 0.06, state->width * 0.1, state->height * 0.08);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glViewport(0, 0, state->width, state->height);

	const int items = state->collected_items;
	const std::string weightText = "Items: " + std::to_string(items);
	font_renderer_.print(weightText, state->width * 0.05f, state->height * 0.105f, .5f, glm::vec3(.95f, .86f, .6f));
	const int money = (int)state->total_cash;
	const std::string loot = "Loot: " + std::to_string(money) + "$";
	font_renderer_.print(loot, state->width * 0.05f, state->height * 0.07f, .5f, glm::vec3(.95f, .86f, .6f));

	if (state->won)
	{
		render_color_.use();
		render_color_.set_vec4("color", glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));
		glDrawArrays(GL_TRIANGLES, 0, 3);
		font_renderer_.print("You made it!", state->width * 0.36f, state->height * 0.48f, 2.0f, glm::vec3(.85f, .68f, .19f));
	}
	else if (state->lost)
	{
		render_color_.use();
		render_color_.set_vec4("color", glm::vec4(0.710, 0.200, 0.180, 1.0f));
		glDrawArrays(GL_TRIANGLES, 0, 3);
		font_renderer_.print(death_msgs_[0], state->width * 0.2f, state->height * 0.48f, 2.0f, glm::vec3(.0f, .0f, .0f));
	}
	if (state->display_collect_item_hint) {
		font_renderer_.print("Click to collect", state->width * 0.42f, state->height * 0.60f, 1.0f, glm::vec3(1.0f, 1.0f, 1.0f));
	}
	glDisable(GL_BLEND);
	OPTICK_POP()
}

void renderer::swap_luminance()
{
	std::swap(luminances_[0], luminances_[1]);
}

renderer::~renderer()
{
	perframe_data_ = nullptr;
	glDeleteTextures(1, &luminance1x1_);
	glDeleteTextures(1, &pattern_);
	glDeleteTextures(1, &hud_);
}