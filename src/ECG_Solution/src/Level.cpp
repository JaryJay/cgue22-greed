#include "Level.h"
#include "Program.h"
#include <meshoptimizer/meshoptimizer.h>
#include <unordered_map>
#include <thread>
#include <optick/optick.h>

level::level(const char* scene_path, const std::shared_ptr<global_state> state, PerFrameData& perframe_data)
: state_(state), perframe_data_(&perframe_data)
{
	std::cout << "import scene from fbx file..." << std::endl;
	OPTICK_PUSH("parse fbx file")
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(scene_path,
	                                         aiProcess_GenSmoothNormals |
	                                         aiProcess_SplitLargeMeshes |
	                                         aiProcess_ImproveCacheLocality |
	                                         aiProcess_RemoveRedundantMaterials |
	                                         aiProcess_FindInvalidData |
	                                         aiProcess_GenUVCoords |
	                                         aiProcess_FlipUVs |
	                                         aiProcess_FixInfacingNormals |
	                                         aiProcess_ValidateDataStructure | 
	                                         0);

	if (!&scene){
		std::cerr << "ERROR: Couldn't load scene" << std::endl;
		exit(EXIT_FAILURE);
	}
	OPTICK_POP()

	OPTICK_PUSH("load meshes")
	std::thread mesh(&level::load_meshes, this, std::ref(scene));
	std::thread light(&level::load_lights, this, std::ref(scene));

	OPTICK_PUSH("load materials")
	load_materials(scene);
	OPTICK_POP()

	// build scene graph and calculate AABBs
	std::cout << "build scene hierarchy..." << std::endl;
	mesh.join();
	OPTICK_POP()
	OPTICK_PUSH("build scene graph")
	traverse_tree(scene->mRootNode, nullptr, &scene_graph_);
	transform_bounding_boxes(&scene_graph_, glm::mat4(1));
	OPTICK_POP()

	OPTICK_PUSH("setup level buffers")
	setup_buffers();
	OPTICK_POP()

	// finalize
	light.join();
	load_shaders();
	for (auto& i : scene_graph_.children)
	{
		if (i.name == "Dynamic")
			dynamic_node_ = &i;
		if (i.name == "Lava1")
			lava_ = &i;
	}

	std::cout << std::endl; // debug breakpoint
}

void level::load_meshes(const aiScene* scene)
{
	global_vertex_offset_ = 0;
	global_index_offset_ = 0;

	meshes_.reserve(scene->mNumMeshes);

	std::cout << "loading meshes..." << std::endl;
	for (size_t i = 0; i < scene->mNumMeshes; i++)
	{
		const aiMesh* mesh = scene->mMeshes[i];
		meshes_.push_back(extract_mesh(mesh));
	}
	frustum_culler::models_loaded = meshes_.size();
}

sub_mesh level::extract_mesh(const aiMesh* mesh)
{

	printf("Mesh [%s] %u\n", mesh->mName.C_Str(), meshes_.size() + 1);
	sub_mesh m;
	m.name = mesh->mName.C_Str();
	m.vertex_offset = global_vertex_offset_;
	m.material_index = mesh->mMaterialIndex;
			
	std::vector<vertex> raw_vertices;
	std::vector <unsigned int> raw_indices;			

	// extract vertices from the aimesh
	for (size_t j = 0; j < mesh->mNumVertices; j++)
	{
		const aiVector3D p = mesh->HasPositions() ? mesh->mVertices[j] : aiVector3D(0.0f);
		const aiVector3D n = mesh->HasNormals() ? mesh->mNormals[j] : aiVector3D(0.0f, 1.0f, 0.0f);
		const aiVector3D t = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][j] : aiVector3D(0.5f, 0.5f, 0.0f);

		vertex vtx =
		{
			p.x, p.y, p.z,
			n.x, n.y, n.z,
			t.x, t.y
		};

		raw_vertices.push_back(vtx);
	}

	//extract indices from the aimesh
	for (size_t j = 0; j < mesh->mNumFaces; j++)
	{
		for (unsigned k = 0; k != mesh->mFaces[j].mNumIndices; k++)
		{
			GLuint index = mesh->mFaces[j].mIndices[k];
			raw_indices.push_back(index);
		}
	}

	// re-index geometry
	std::vector<unsigned int> remap(raw_indices.size());
	size_t vertex_count = meshopt_generateVertexRemap(remap.data(), raw_indices.data(), raw_indices.size(), raw_vertices.data(), raw_indices.size(), vtx_stride);

	std::vector <unsigned int> opt_indices(raw_indices.size());
	std::vector<vertex> opt_vertices(vertex_count);

	meshopt_remapIndexBuffer(opt_indices.data(), raw_indices.data(), raw_indices.size(), remap.data());
	meshopt_remapVertexBuffer(opt_vertices.data(), raw_vertices.data(), raw_vertices.size(), vtx_stride, remap.data());

	// further optimize geometry
	meshopt_optimizeVertexCache(opt_indices.data(), opt_indices.data(), raw_indices.size(), vertex_count);
	meshopt_optimizeOverdraw(opt_indices.data(), opt_indices.data(), raw_indices.size(), &opt_vertices[0].px, vertex_count, vtx_stride, 1.05f);
	meshopt_optimizeVertexFetch(opt_vertices.data(), opt_indices.data(), raw_indices.size(), opt_vertices.data(), vertex_count, vtx_stride);

	m.vertex_count = opt_vertices.size();

	std::vector<float> result_vertices;
	for (const auto& vertex : opt_vertices)
	{
		result_vertices.push_back(vertex.px); result_vertices.push_back(vertex.py); result_vertices.push_back(vertex.pz);
		result_vertices.push_back(vertex.nx); result_vertices.push_back(vertex.ny); result_vertices.push_back(vertex.nz);
		result_vertices.push_back(vertex.tx); result_vertices.push_back(vertex.ty);
	}

	std::vector<std::vector<unsigned int>> LODs;
	generate_lods(opt_indices, result_vertices, LODs);

	vertices.insert(vertices.end(), result_vertices.begin(), result_vertices.end());

	auto index_sum = 0;
	for (auto& LOD : LODs)
	{
		m.index_count.push_back(LOD.size());
		m.index_offset.push_back(global_index_offset_ + index_sum);
		index_sum += LOD.size();
		indices_.insert(indices_.end(), LOD.begin(), LOD.end());
	}

	global_vertex_offset_ += m.vertex_count;
	global_index_offset_ += index_sum;
	return m;
}

void level::generate_lods(std::vector<unsigned int>& indices,const std::vector<float>& vertices, std::vector<std::vector<unsigned int>>& LODs)
{
	const size_t vertices_count_in = vertices.size() / 8;
	size_t target_indices_count = indices.size();

	uint8_t lod = 1;

#ifdef _DEBUG
	printf("LOD0: %i indices   \n", static_cast<int>(indices.size()));
#endif

	LODs.push_back(indices);

	constexpr auto target = 1024; // for testing, final should be 1024
	while (target_indices_count > target && lod < 8)
	{
		target_indices_count = indices.size() / 2;

		bool sloppy = false;

		size_t num_opt_indices = meshopt_simplify(
			indices.data(),
			indices.data(), (unsigned int)indices.size(),
			vertices.data(), vertices_count_in,
			sizeof(float) * 8,
			target_indices_count, 0.02f);

		// cannot simplify further
		if (static_cast<size_t>(num_opt_indices * 1.1f) > indices.size())
		{
			if (lod > 1)
			{
				// try harder
				num_opt_indices = meshopt_simplifySloppy(
					indices.data(),
					indices.data(), indices.size(),
					vertices.data(), vertices_count_in,
					sizeof(float) * 8,
					target_indices_count);
				sloppy = true;
				if (num_opt_indices == indices.size()) break;
			}
			else
				break;
		}

		indices.resize(num_opt_indices);

		meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices_count_in);

#ifdef _DEBUG
		printf("LOD%i: %i indices %s   \n", static_cast<int>(lod), static_cast<int>(num_opt_indices), sloppy ? "[sloppy]" : "");
#endif

		lod++;

		LODs.push_back(indices);
	}
}

bounding_box level::compute_bounds_of_mesh(const sub_mesh& mesh) const
{
	const auto num_indices = mesh.vertex_count;

	glm::vec3 vmin(std::numeric_limits<float>::max());
	glm::vec3 vmax(std::numeric_limits<float>::lowest());

	for (auto i = 0; i != num_indices; i++)
	{
		const auto vertex_offset = (mesh.vertex_offset + i) * 8;
		const float* vf = &vertices[vertex_offset];

		vmin = glm::min(vmin, glm::vec3(vf[0], vf[1], vf[2]));
		vmax = glm::max(vmax, glm::vec3(vf[0], vf[1], vf[2]));
	}
	return {vmin, vmax };
}

void level::transform_bounding_boxes(hierarchy* node, glm::mat4 global_transform)
{
	boolean is_leaf = !(node->model_index == -1);
	glm::mat4 M = global_transform * node->get_node_matrix();

	if (is_leaf) // transform model bounds to world coordinates
	{
		bounding_box bounds = node->model_bounds;
		bounds.min_ = M * glm::vec4(bounds.min_, 1.0f);
		bounds.max_ = M * glm::vec4(bounds.max_, 1.0f);
		node->node_bounds = bounding_box(bounds.min_, bounds.max_);
	}

	// transform all child nodes bounds
	for (auto& i : node->children)
	{
		transform_bounding_boxes(&i, M);
	}
	
	if (!is_leaf) // calculate the node bound from childrens bounds
	{
		glm::vec3 vmin(std::numeric_limits<float>::max());
		glm::vec3 vmax(std::numeric_limits<float>::lowest());

		for (auto& i : node->children)
		{
			glm::vec3 cmin = i.node_bounds.min_;
			glm::vec3 cmax = i.node_bounds.max_;

			vmin = glm::min(vmin, cmin);
			vmax = glm::max(vmax, cmax);
		}

		auto bounds = bounding_box(vmin, vmax);
		bounds.min_ = M * glm::vec4(bounds.min_, 1.0f);
		bounds.max_ = M * glm::vec4(bounds.max_, 1.0f);
		node->node_bounds = bounding_box(bounds.min_, bounds.max_);
	}
}

void level::load_materials(const aiScene* scene)
{
	std::cout << "loading materials..." << std::endl;

	for (size_t m = 0; m < scene->mNumMaterials; m++)
	{
		const aiMaterial* mm = scene->mMaterials[m];

		printf("Material [%s] %u\n", mm->GetName().C_Str(), m + 1);

		aiString path;

		if (aiGetMaterialTexture(mm, aiTextureType_BASE_COLOR, 0, &path) == AI_SUCCESS)
		{
			const auto albedo_map = std::string(path.C_Str());
		}

		//	all other materials can be found with: aiTextureType_NORMAL_CAMERA/_METALNESS/_DIFFUSE_ROUGHNESS/_AMBIENT_OCCLUSION

		if(path.length != 0)
		{
			material mat;
			material::create(path.C_Str(), mm->GetName().C_Str(), mat);
			materials_.push_back(mat);
			render_item item;
			item.material = mm->GetName().C_Str();
			render_queue_shadow_.push_back(item);
			render_queue_scene_.push_back(item);
		} else //default
		{
			material mat;
			mat.type = invisible;
			//create("textures/default/albedo.jpg", "default", mat);
			materials_.push_back(mat);
			render_item item;
			item.material = mm->GetName().C_Str();
			render_queue_shadow_.push_back(item);
			render_queue_scene_.push_back(item);
		}
	}
}

void level::traverse_tree(const aiNode* n, hierarchy* parent, hierarchy* node)
{
	// set trivial node variables
	const glm::mat4 M = to_glm_mat4(n->mTransformation);
	node->name = n->mName.C_Str();
	node->parent = parent;

	// add a all mesh indices to this node (assumes only 1 mesh per node) and calculate bounds in model space
	if (n->mNumMeshes > 0)
	{
		node->model_index = n->mMeshes[0];
		node->model_bounds = compute_bounds_of_mesh(meshes_[n->mMeshes[0]]);
	}

	// set translation, rotation and scale of this node
	glm::decompose(M, node->TRS.scale, node->TRS.rotation, node->TRS.translate, glm::vec3(), glm::vec4());
	node->TRS.rotation = glm::normalize(glm::conjugate(node->TRS.rotation));

	// travers child nodes
	for (size_t i = 0; i < n->mNumChildren; i++)
	{
		if (strcmp(n->mChildren[i]->mName.C_Str(),"Lights") != 0 && // skip lights
			(n->mChildren[i]->mNumChildren > 0 || n->mChildren[i]->mNumMeshes > 0)) // skip empty nodes
		{
		hierarchy child;
		traverse_tree(n->mChildren[i], node, &child);
		node->children.push_back(child);
		}
	}
}

glm::mat4 level::to_glm_mat4(const aiMatrix4x4& mat)
{
	glm::mat4 result;
	result[0][0] = static_cast<float>(mat.a1); result[0][1] = static_cast<float>(mat.b1);  result[0][2] = static_cast<float>(mat.c1); result[0][3] = static_cast<float>(mat.d1);
	result[1][0] = static_cast<float>(mat.a2); result[1][1] = static_cast<float>(mat.b2);  result[1][2] = static_cast<float>(mat.c2); result[1][3] = static_cast<float>(mat.d2);
	result[2][0] = static_cast<float>(mat.a3); result[2][1] = static_cast<float>(mat.b3);  result[2][2] = static_cast<float>(mat.c3); result[2][3] = static_cast<float>(mat.d3);
	result[3][0] = static_cast<float>(mat.a4); result[3][1] = static_cast<float>(mat.b4);  result[3][2] = static_cast<float>(mat.c4); result[3][3] = static_cast<float>(mat.d4);
	return result;
}

void level::setup_buffers()
{
	std::cout << "setup buffers..." << std::endl;

	const buffer vbo(0);
	vbo.reserve_memory(static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
	const buffer ebo(0);
	ebo.reserve_memory(static_cast<GLsizeiptr>(indices_.size() * sizeof(GLuint)), indices_.data());

	glCreateVertexArrays(1, &vao_);
	glVertexArrayElementBuffer(vao_, ebo.get_id());
	glVertexArrayVertexBuffer(vao_, 0, vbo.get_id(), 0, sizeof(glm::vec3) + sizeof(glm::vec3) + sizeof(glm::vec2));
	// position
	glEnableVertexArrayAttrib(vao_, 0);
	glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribBinding(vao_, 0, 0);
	// normal
	glEnableVertexArrayAttrib(vao_, 1);
	glVertexArrayAttribFormat(vao_, 1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3));
	glVertexArrayAttribBinding(vao_, 1, 0);
	// uv
	glEnableVertexArrayAttrib(vao_, 2);
	glVertexArrayAttribFormat(vao_, 2, 2, GL_FLOAT, GL_TRUE, sizeof(glm::vec3) + sizeof(glm::vec3));
	glVertexArrayAttribBinding(vao_, 2, 0);

	ibo_.reserve_memory(static_cast<GLsizeiptr>(meshes_.size() * sizeof(draw_elements_indirect_command)), nullptr);
	matrix_ssbo_.reserve_memory(4, static_cast<GLsizeiptr>(meshes_.size() * sizeof(glm::mat4)), nullptr);
	tex_ssbo_.reserve_memory(5, static_cast<GLsizeiptr>(materials_.size() * sizeof(material)), materials_.data());
}

void level::load_lights(const aiScene* scene) {

	std::cout << "loading lights..." << std::endl;

	// collect light sources
	std::unordered_map<std::string, aiLight*> light_map;
	for (size_t i = 0; i < scene->mNumLights; i++)
	{
		aiLight* light = scene->mLights[i];
		light_map.insert({light->mName.C_Str(), light});
	}

	// find the light node
	aiNode* lights = nullptr;
	for (size_t i = 0; i < scene->mRootNode->mNumChildren; i++)
	{
		if (strcmp(scene->mRootNode->mChildren[i]->mName.C_Str(), "Lights") == 0)
		{
			lights = scene->mRootNode->mChildren[i];
			break;
		}
	}

	// extract light soruces
	for (size_t i = 0; i < lights->mNumChildren; i++)
	{
		aiNode* child = lights->mChildren[i];

		if (child->mNumChildren == 1) // directional light
		{
			aiNode* pre = child; // get prerotation
			glm::quat pre_rot = glm::quat_cast(to_glm_mat4(pre->mTransformation));

			aiNode* post = pre->mChildren[0]; // get prerotation
			glm::quat post_rot = glm::quat_cast(to_glm_mat4(post->mTransformation));

			aiNode* lig = post->mChildren[0]; // get light
			std::string name = lig->mName.C_Str();
			aiLight* light = light_map.at(name);
			assert(light->mType == aiLightSource_DIRECTIONAL);
			glm::quat lig_rot = glm::quat_cast(to_glm_mat4(lig->mTransformation));


			glm::quat finalRot = pre_rot * lig_rot * post_rot;

			const aiVector3D dir = light->mDirection;
			const aiColor3D col = light->mColorDiffuse;

			glm::vec3 direction = glm::vec3(dir.x, dir.y, dir.z);	
			direction = -glm::rotate(finalRot, direction);	// light direction gets inverted in shader
			glm::vec4 intensity = glm::vec4(col.r, col.g, col.b, 1.0f) * 3.0f; // increase light intensity (maya normalizes it)

			this->lights_.directional.push_back(directional_light{glm::vec4(direction,1.0f), intensity});
		}
		else // point light
		{
			std::string name = child->mName.C_Str();
			aiLight* light = light_map.at(name);
			assert(light->mType == aiLightSource_POINT);
			glm::mat4 M = to_glm_mat4(child->mTransformation);

			const aiColor3D col = light->mColorDiffuse;
			glm::vec4 position = glm::vec4(0,0,0,1.0f);

			position = M * position;
			glm::vec4 intensity = glm::vec4(col.r, col.g, col.b, 1.0f);

			this->lights_.point.push_back(positional_light{ position, intensity });
		}
	}
}

void level::load_shaders()
{
	aabb_viewer_ = std::make_unique<program>();
	Shader bounds_vert("../../assets/shaders/Testing/AABBviewer.vert");
	Shader bounds_frag("../../assets/shaders/Testing/AABBviewer.frag");
	aabb_viewer_->build_from(bounds_vert, bounds_frag);

	frustumviewer_ = std::make_unique<program>();
	Shader frustum_vert("../../assets/shaders/Testing/Frustumviewer.vert");
	frustumviewer_->build_from(frustum_vert, bounds_frag);
}

std::vector<physics_mesh> level::get_rigid()
{
	hierarchy* rigid_node = nullptr;
	for (auto& i : scene_graph_.children)
	{
		if (i.name == "Rigid") {
			rigid_node = &i;
		}
	}
	collect_rigid_physic_meshes(rigid_node, glm::mat4(1));
	return rigid_;
}

std::vector<physics_mesh> level::get_dynamic()
{
	collect_dynamic_physic_meshes(dynamic_node_, glm::mat4(1));
	return dynamic_;
}

void level::collect_rigid_physic_meshes(hierarchy* node, glm::mat4 global_transform)
{
	glm::mat4 node_matrix = global_transform * node->get_node_matrix();
	
	if (node->model_index != -1)
	{
		uint32_t model_index = node->model_index;
		uint32_t vtx_offset = meshes_[model_index].vertex_offset;
		uint32_t vtx_count = meshes_[model_index].vertex_count;
		physics_mesh phy_mesh;

		transformation trs;
		glm::decompose(node_matrix, trs.scale, trs.rotation, trs.translate, glm::vec3(), glm::vec4());
		trs.rotation = glm::normalize(glm::conjugate(trs.rotation));

		for (uint32_t j = 0; j != vtx_count; j++)
		{
			auto vertex_offset = (vtx_offset + j) * 8;
			const float* vf = &vertices[vertex_offset];

			phy_mesh.vtx_positions.push_back(vf[0]);
			phy_mesh.vtx_positions.push_back(vf[1]);
			phy_mesh.vtx_positions.push_back(vf[2]);
		}

		phy_mesh.model_trs = trs;
		phy_mesh.node = node;
		rigid_.push_back(phy_mesh);
	}

	for (auto& i : node->children)
	{
		collect_rigid_physic_meshes(&i, node_matrix);
	}
}

void level::collect_dynamic_physic_meshes(hierarchy* node, glm::mat4 global_transform)
{
	glm::mat4 node_matrix = global_transform * node->get_node_matrix();

	if (node->model_index != -1)
	{
		uint32_t model_index = node->model_index;
		uint32_t vtx_offset = meshes_[model_index].vertex_offset;
		uint32_t vtx_count = meshes_[model_index].vertex_count;
		physics_mesh phy_mesh;

		transformation trs;
		glm::decompose(node_matrix, trs.scale, trs.rotation, trs.translate, glm::vec3(), glm::vec4());
		trs.rotation = glm::normalize(glm::conjugate(trs.rotation));

		for (uint32_t j = 0; j != vtx_count; j++)
		{
			auto vertex_offset = (vtx_offset + j) * 8;
			const float* vf = &vertices[vertex_offset];

			phy_mesh.vtx_positions.push_back(vf[0]);
			phy_mesh.vtx_positions.push_back(vf[1]);
			phy_mesh.vtx_positions.push_back(vf[2]);
		}

		phy_mesh.model_trs = trs;
		phy_mesh.node = node;
		dynamic_.push_back(phy_mesh);
	}

	for (auto& i : node->children)
	{
		collect_dynamic_physic_meshes(&i, node_matrix);
	}
}

void level::draw_scene() {

	OPTICK_PUSH("update scene")
	// update view frustum
	if (!state_->freeze_cull)
	{
		OPTICK_PUSH("update frustum culler")
		frustum_culler::cull_view_proj = perframe_data_->view_proj;
		frustum_culler::get_frustum_planes(frustum_culler::cull_view_proj, frustum_culler::frustum_planes);
		frustum_culler::get_frustum_corners(frustum_culler::cull_view_proj, frustum_culler::frustum_corners);
		OPTICK_POP()
	}
	OPTICK_POP()


	// draw mesh
	OPTICK_PUSH("draw scene")
	for (size_t i = 0; i < render_queue_scene_.size(); i++)
	{
		if(materials_[i].type == invisible) continue;
		matrix_ssbo_.update(static_cast<GLsizeiptr>(sizeof(glm::mat4) * render_queue_scene_[i].model_matrices.size()), render_queue_scene_[i].model_matrices.data());
		ibo_.update(static_cast<GLsizeiptr>(render_queue_scene_[i].commands.size() * sizeof(draw_elements_indirect_command)), render_queue_scene_[i].commands.data());

		const GLuint textures[] = {materials_[i].albedo_, materials_[i].normal_, materials_[i].metal_,
			materials_[i].rough_, materials_[i].ao_, materials_[i].emissive_ };
		glBindTextures(0, 6, textures);

		if (render_queue_scene_[i].material == "Lava_1") // may the GL gods have mercy with this monstrosity TODO
		{
			GLint prog = 0;
			glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
			glUniform1i(glGetUniformLocation(prog, "vtx_animation"), 1);
		}
		
		/// mode - draw triangles from every 3 indices
		/// type - data type of the indices vector
		/// indirect - offset into commands buffer, which is zero
		/// drawcount - is the number of draw calls that should be generated
		/// stride - because the commands are packed tightly aka just as descriped in the GL specs
		glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, static_cast<GLvoid*>(nullptr), static_cast<GLsizei>(render_queue_scene_[i].commands.size()), 0);

		if (render_queue_scene_[i].material == "Lava_1") // may the GL gods have mercy with this monstrosity TODO
		{
			GLint prog = 0;
			glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
			glUniform1i(glGetUniformLocation(prog, "vtx_animation"), 0);
		}
	}
	OPTICK_POP()

#ifdef _DEBUG
	if (state_->cull_debug) // bounding box & frustum culling debug view
	{
		OPTICK_PUSH("draw debug AABB")
		glDisable(GL_CULL_FACE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glEnable(GL_BLEND);
		aabb_viewer_->use();
		aabb_viewer_->set_vec4("lineColor", glm::vec4(0.0f,1.0f,0.0f, .1f));
			draw_aabbs(scene_graph_); // draw AABBs
		frustumviewer_->use();
		frustumviewer_->set_vec4("lineColor", glm::vec4(1.0f, 1.0f, 0.0f, .1f));
		frustumviewer_->set_vec3("corner0", frustum_culler::frustum_corners[0]);
		frustumviewer_->set_vec3("corner1", frustum_culler::frustum_corners[1]);
		frustumviewer_->set_vec3("corner2", frustum_culler::frustum_corners[2]);
		frustumviewer_->set_vec3("corner3", frustum_culler::frustum_corners[3]);
		frustumviewer_->set_vec3("corner4", frustum_culler::frustum_corners[4]);
		frustumviewer_->set_vec3("corner5", frustum_culler::frustum_corners[5]);
		frustumviewer_->set_vec3("corner6", frustum_culler::frustum_corners[6]);
		frustumviewer_->set_vec3("corner7", frustum_culler::frustum_corners[7]);
			glDrawArrays(GL_TRIANGLES, 0, 36); // draw frustum
		glDisable(GL_BLEND);
		glEnable(GL_CULL_FACE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		
		// output frustum culling information for debugging every 2 seconds
		if (state_->cull)
		{
			frustum_culler::seconds_since_flush += perframe_data_->delta_time.x;
			if (frustum_culler::seconds_since_flush >= 2)
			{
				std::cout << "Models Loaded: " << frustum_culler::models_loaded << ", Models rendered: " << frustum_culler::models_visible
					<< ", Models culled: " << frustum_culler::models_loaded - frustum_culler::models_visible << "\n";
				frustum_culler::seconds_since_flush = 0;
			}
		}
		OPTICK_POP()
	}
#endif
}

void level::draw_scene_shadow_map()
{
	OPTICK_PUSH("update scene")

	// recalculate bounds & set lod uniforms
	if (perframe_data_->delta_time.y > 60.0f) lava_->TRS.translate.y += perframe_data_->delta_time.x * 1.0f; //TODO
	OPTICK_PUSH("transform bounding boxes")
	transform_bounding_boxes(lava_, glm::mat4(1));
	OPTICK_POP()
	OPTICK_PUSH("update frustum culler uniform")
	lod_system::near_plane = perframe_data_->ssao1.z;
	lod_system::view_pos = perframe_data_->view_pos;
	glm::mat4 vp = glm::transpose(perframe_data_->view_proj);
	lod_system::view_dir = vp[3];
	OPTICK_POP()

	// flatten tree
	OPTICK_PUSH("build render queue")
	reset_queue();
	build_render_queue(&scene_graph_, glm::mat4(1));
	OPTICK_POP()
	OPTICK_POP()

	// draw mesh
	OPTICK_PUSH("draw scene")
	glBindVertexArray(vao_);

	glDisable(GL_CULL_FACE);
	for (auto& item : render_queue_shadow_)
	{
		matrix_ssbo_.update(static_cast<GLsizeiptr>(sizeof(glm::mat4) * item.model_matrices.size()), item.model_matrices.data());
		ibo_.update(static_cast<GLsizeiptr>(item.commands.size() * sizeof(draw_elements_indirect_command)), item.commands.data());
		glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, static_cast<GLvoid*>(nullptr), static_cast<GLsizei>(item.commands.size()), 0);
	}
	glEnable(GL_CULL_FACE);
	OPTICK_POP()
}

void level::build_render_queue(const hierarchy* node, const glm::mat4 global_transform) {
	if (!node->game_properties.is_active)
		return;
	

	const glm::mat4 node_matrix = global_transform * node->get_node_matrix();
	if (node->model_index != -1)
	{
		OPTICK_PUSH("add model to queue")
		const uint32_t mesh_index = node->model_index;
		const uint32_t material_index = meshes_[mesh_index].material_index;
		const uint32_t model_index = render_queue_shadow_[material_index].model_matrices.size();

		uint32_t LOD = 0;
		

		// add to shadow queue ------------------------------------------
		const uint32_t count = meshes_[mesh_index].index_count[LOD];	
		const uint32_t instanceCount = 1;
		const uint32_t firstIndex = meshes_[mesh_index].index_offset[LOD]; 
		const uint32_t baseVertex = meshes_[mesh_index].vertex_offset; 
		const uint32_t baseInstance = material_index + (static_cast<uint32_t>(model_index) << 16);

		draw_elements_indirect_command cmd= draw_elements_indirect_command{
			count,
			instanceCount,
			firstIndex,
			baseVertex,
			baseInstance };

		render_queue_shadow_[material_index].commands.push_back(cmd);
		render_queue_shadow_[material_index].model_matrices.push_back(node_matrix);

		// add to scene queue ---------------------------------------------
		if (state_->cull)
		{
			if (!frustum_culler::is_box_in_frustum(frustum_culler::frustum_planes, frustum_culler::frustum_corners, node->node_bounds))
				cmd.instanceCount_ = 0;
		}
		
		LOD = lod_system::decide_lod(meshes_[mesh_index].index_count.size(), node->node_bounds);
		cmd.count_ = meshes_[mesh_index].index_count[LOD];
		cmd.firstIndex_ = meshes_[mesh_index].index_offset[LOD];

		render_queue_scene_[material_index].commands.push_back(cmd);
		render_queue_scene_[material_index].model_matrices.push_back(node_matrix);


		frustum_culler::models_visible += cmd.instanceCount_;
		OPTICK_POP()
	}
	

	for (const auto& hierarchy : node->children)
	{
		build_render_queue(&hierarchy, node_matrix);
	}
}

void level::reset_queue()
{

	for (auto& item : render_queue_shadow_)
	{
		item.commands.clear();
		item.commands.reserve(frustum_culler::models_visible);
		item.model_matrices.clear();
		item.model_matrices.reserve(frustum_culler::models_visible);
	}
	for (auto& item : render_queue_scene_)
	{
		item.commands.clear();
		item.commands.reserve(frustum_culler::models_visible);
		item.model_matrices.clear();
		item.model_matrices.reserve(frustum_culler::models_visible);
	}
	

	frustum_culler::models_visible = 0;
}



void level::draw_aabbs(const hierarchy node)
{
	if (node.model_index != -1)
	{
		bounding_box bounds = node.node_bounds;
		aabb_viewer_->set_vec3("min", node.node_bounds.min_);
		aabb_viewer_->set_vec3("max", node.node_bounds.max_);

		glDrawArrays(GL_TRIANGLES, 0, 36);
	}

	for (const auto& hierarchy : node.children)
	{
		draw_aabbs(hierarchy);
	}
}

glm::mat4 level::get_tight_scene_frustum(glm::mat4 light_view) const
{
	glm::vec3 min = scene_graph_.node_bounds.min_;
	glm::vec3 max = scene_graph_.node_bounds.max_;

	glm::vec3 corners[] = {
			glm::vec3(min.x, min.y, min.z),
			glm::vec3(min.x, max.y, min.z),
			glm::vec3(min.x, min.y, max.z),
			glm::vec3(min.x, max.y, max.z),
			glm::vec3(max.x, min.y, min.z),
			glm::vec3(max.x, max.y, min.z),
			glm::vec3(max.x, min.y, max.z),
			glm::vec3(max.x, max.y, max.z),
	};
	for (auto& v : corners)
		v = glm::vec3(light_view * glm::vec4(v, 1.0f));

	glm::vec3 vmin(std::numeric_limits<float>::max());
	glm::vec3 vmax(std::numeric_limits<float>::lowest());

	for (auto& corner : corners)
	{
		vmin = glm::min(vmin, corner);
		vmax = glm::max(vmax, corner);
	}
	min = vmin;
	max = vmax;

	return glm::ortho(min.x, max.x, min.y, max.y, -max.z, -min.z);
}

void level::release() const
{
	glDeleteVertexArrays(1, &vao_);

	for (auto material : materials_)
	{
		material::clear(material);
	}
}
