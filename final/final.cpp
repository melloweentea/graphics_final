#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

// GLTF model loader
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// #define TINYGLTF_NOEXCEPTION // optional. disable exception handling.
#include <tiny_gltf.h>

#include <render/shader.h>

// #define STB_IMAGE_IMPLEMENTATION
// #include <stb/stb_image.h>

#include <random>
#include <vector>
#include <iostream>
#include <iomanip>
#define _USE_MATH_DEFINES
#include <math.h>

// for model loading
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

static GLFWwindow *window;
static int windowWidth = 1024;
static int windowHeight = 768;

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mode);

// OpenGL camera view parameters
static glm::vec3 eye_center(-2.91998, -2.91998, 99.9574);
static glm::vec3 lookat(0, 0, 0);
static glm::vec3 up(0, 1, 0);
static glm::vec3 forward = glm::normalize(lookat - eye_center);

static float cameraSpeed = 1.0f;

static float FoV = 45.0f;
static float zNear = 0.1f;
static float zFar = 5000.0f; 
// skybox params
// glm::float32 FoV = 45;
// glm::float32 zNear = 0.1f; 
// glm::float32 zFar = 5000.0f;

// View control 
static float viewAzimuth = 0.f;
static float viewPolar = 0.f;
// static float viewDistance = 300.0f;
static float viewDistance = 100.0f;

//hdr fbo 
static GLuint hdrFBO;
static GLuint colorBuffer;

//ping pong blurring fbo 
static GLuint pingpongFBO[2];
static GLuint pingpongColorbuffers[2];

//lighting control 
const glm::vec3 wave500(0.0f, 255.0f, 146.0f);
const glm::vec3 wave600(255.0f, 190.0f, 0.0f);
const glm::vec3 wave700(205.0f, 0.0f, 0.0f);
static glm::vec3 lightIntensity = 5.0f * (8.0f * wave500 + 15.6f * wave600 + 18.4f * wave700);
// static glm::vec3 lightPosition(5.36495f, 14.3552f, -212.269f);
static glm::vec3 lightPosition(5.36495f, 54.3552f, -212.269f);
// static glm::vec3 lightPosition(0.0f, 100.0f, 0.0f);
static glm::vec3 lightLookAt(0.0f, 0.0f, 0.0f); 

//shadow mapping 
static glm::vec3 lightUp(0, 1, 0);
static int shadowMapWidth = 1024;
static int shadowMapHeight = 1024;
static GLuint fbo;
static GLuint depthTex;

static float depthFoV = 90.0f;
static float depthNear = 40.0f;
static float depthFar = 750.0f; 

static bool saveDepth = false;

// Animation 
static bool playAnimation = true;
static float playbackSpeed = 2.0f;

// shadow mapping 
static void saveDepthTexture(GLuint fbo, std::string filename) {
    int width = shadowMapWidth;
    int height = shadowMapHeight;
	if (shadowMapWidth == 0 || shadowMapHeight == 0) {
		width = windowWidth;
		height = windowHeight;
	}
    int channels = 3; 
    
    std::vector<float> depth(width * height);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_DEPTH_COMPONENT);
    glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::vector<unsigned char> img(width * height * 3);
    for (int i = 0; i < width * height; ++i) img[3*i] = img[3*i+1] = img[3*i+2] = depth[i] * 255;

    stbi_write_png(filename.c_str(), width, height, channels, img.data(), width * channels);
}

// creating framebuffer object for shadow mapping
static void initializeFBO(int width, int height) {
	// creating framebuffer object
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	// creating depth texture
	glGenTextures(1, &depthTex);
	glBindTexture(GL_TEXTURE_2D, depthTex);
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_DEPTH_COMPONENT,   // internal format
		width, height,
		0,
		GL_DEPTH_COMPONENT,   // format
		GL_FLOAT,             // type
		NULL
	);

	//texture settings for depth texture
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// attaching depth texture to framebuffer object
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);

	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);	

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cout << "FBO not complete!" << std::endl;
	}

	//unbind 
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// 3d Model loading
struct Model {
	// Shader variable IDs
	GLuint mvpMatrixID;
	GLuint modelShader;
	GLuint depthShader;

	tinygltf::Model model;

	//for position and rotation in render function 
	glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Euler angles
    float scale = 1.0f;

	//for animation
	float rotationAngle = 0.0f;

	// Each VAO corresponds to each mesh primitive in the GLTF model
	struct PrimitiveObject {
		GLuint vao;
		std::map<int, GLuint> vbos;
		GLuint textureID; // Add this to store the texture handle
	};
	std::vector<PrimitiveObject> primitiveObjects;

	bool loadModel(tinygltf::Model &model, const char *filename) {
		tinygltf::TinyGLTF loader;
		std::string err;
		std::string warn;

		bool res = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
		if (!warn.empty()) {
			std::cout << "WARN: " << warn << std::endl;
		}

		if (!err.empty()) {
			std::cout << "ERR: " << err << std::endl;
		}

		if (!res)
			std::cout << "Failed to load glTF: " << filename << std::endl;
		else
			std::cout << "Loaded glTF: " << filename << std::endl;

		return res;
	}

	void initialize(const char* path) {
		// Modify your path if needed

		if (!loadModel(model, path)) {
			return;
		}

		// Prepare buffers for rendering 
		primitiveObjects = bindModel(model);

		// Create and compile our GLSL program from the shaders
		modelShader = LoadShadersFromFile("../final/model.vert", "../final/model.frag");
		if (modelShader == 0)
		{
			std::cerr << "Failed to load shaders." << std::endl;
		}

		depthShader = LoadShadersFromFile("../final/depth.vert", "../final/depth.frag");
		if (depthShader == 0)
		{
			std::cerr << "Failed to load shaders." << std::endl;
		}

		// Get a handle for GLSL variables
		mvpMatrixID = glGetUniformLocation(modelShader, "MVP");
	}

	void bindMesh(std::vector<PrimitiveObject> &primitiveObjects,
				tinygltf::Model &model, tinygltf::Mesh &mesh) {

		std::map<int, GLuint> vbos;
		for (size_t i = 0; i < model.bufferViews.size(); ++i) {
			const tinygltf::BufferView &bufferView = model.bufferViews[i];

			int target = bufferView.target;
			
			if (bufferView.target == 0) { 
				continue;
			}

			const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
			GLuint vbo;
			glGenBuffers(1, &vbo);
			glBindBuffer(target, vbo);
			glBufferData(target, bufferView.byteLength,
						&buffer.data.at(0) + bufferView.byteOffset, GL_STATIC_DRAW);
			
			vbos[i] = vbo;
		}

		// Each mesh can contain several primitives (or parts), each we need to 
		// bind to an OpenGL vertex array object
		//std::cout << "Mesh primitives: " << mesh.primitives.size() << std::endl;
		for (size_t i = 0; i < mesh.primitives.size(); ++i) {

			tinygltf::Primitive primitive = mesh.primitives[i];
			tinygltf::Accessor indexAccessor = model.accessors[primitive.indices];

			PrimitiveObject primitiveObject;

			GLuint vao;
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);

			for (auto &attrib : primitive.attributes) {
				tinygltf::Accessor accessor = model.accessors[attrib.second];
				int byteStride =
					accessor.ByteStride(model.bufferViews[accessor.bufferView]);
				glBindBuffer(GL_ARRAY_BUFFER, vbos[accessor.bufferView]);

				int size = 1;
				if (accessor.type != TINYGLTF_TYPE_SCALAR) {
					size = accessor.type;
				}

				// TODO: Remember to set the vaa index according to 
				// the buffer in the vertex shader. 
				int vaa = -1;
				std::cout << "Attrib: " << attrib.first << std::endl;
				if (attrib.first.compare("POSITION") == 0) vaa = 0;
				if (attrib.first.compare("TEXCOORD_0") == 0) vaa = 1;
				
				if (vaa > -1) {
					glEnableVertexAttribArray(vaa);
					glVertexAttribPointer(vaa, size, accessor.componentType,
										accessor.normalized ? GL_TRUE : GL_FALSE,
										byteStride, BUFFER_OFFSET(accessor.byteOffset));
				} else {
					std::cout << "vaa missing: " << attrib.first << std::endl;
				}
			}

			// 1. Get the material index from the current primitive (assuming you are in a loop)
			int matIndex = primitive.material; 

			// Only proceed if the primitive actually has a material assigned
			if (matIndex != -1) {
				tinygltf::Material &material = model.materials[matIndex];
				
				// 2. Access the baseColorTexture index from the PBR properties
				int texIndex = material.pbrMetallicRoughness.baseColorTexture.index;

				// 3. Check if the texture exists
				if (texIndex > -1) {
					tinygltf::Texture &tex = model.textures[texIndex];

					if (tex.source > -1) {
						tinygltf::Image &image = model.images[tex.source];

						GLuint texid;
						glGenTextures(1, &texid);
						glBindTexture(GL_TEXTURE_2D, texid);

						primitiveObject.textureID = texid;

						glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

						// 4. Use Sampler settings from the file instead of hardcoded REPEAT
						if (tex.sampler != -1) {
							tinygltf::Sampler &sampler = model.samplers[tex.sampler];
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampler.minFilter != -1 ? sampler.minFilter : GL_LINEAR);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampler.magFilter != -1 ? sampler.magFilter : GL_LINEAR);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sampler.wrapS);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sampler.wrapT);
						} else {
							// Default fallback
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
						}

						GLenum format = GL_RGBA;
						if (image.component == 1) format = GL_RED;
						else if (image.component == 2) format = GL_RG;
						else if (image.component == 3) format = GL_RGB;

						GLenum type = GL_UNSIGNED_BYTE;
						if (image.bits == 16) type = GL_UNSIGNED_SHORT;

						// 5. Use GL_SRGB8_ALPHA8 for baseColor so colors aren't washed out
						glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height, 0, format, type, &image.image.at(0));

						glGenerateMipmap(GL_TEXTURE_2D);
						// Re-enable mipmap filtering if you generated them
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
					}
				}
			}

			// Record VAO for later use
			primitiveObject.vao = vao;
			primitiveObject.vbos = vbos;
			primitiveObjects.push_back(primitiveObject);

			glBindVertexArray(0);
		}
	}

	void bindModelNodes(std::vector<PrimitiveObject> &primitiveObjects, 
						tinygltf::Model &model,
						tinygltf::Node &node) {
		// Bind buffers for the current mesh at the node
		if ((node.mesh >= 0) && (node.mesh < model.meshes.size())) {
			bindMesh(primitiveObjects, model, model.meshes[node.mesh]);
		}

		// Recursive into children nodes
		for (size_t i = 0; i < node.children.size(); i++) {
			assert((node.children[i] >= 0) && (node.children[i] < model.nodes.size()));
			bindModelNodes(primitiveObjects, model, model.nodes[node.children[i]]);
		}
	}

	std::vector<PrimitiveObject> bindModel(tinygltf::Model &model) {
		std::vector<PrimitiveObject> primitiveObjects;

		const tinygltf::Scene &scene = model.scenes[model.defaultScene];
		for (size_t i = 0; i < scene.nodes.size(); ++i) {
			assert((scene.nodes[i] >= 0) && (scene.nodes[i] < model.nodes.size()));
			bindModelNodes(primitiveObjects, model, model.nodes[scene.nodes[i]]);
		}

		return primitiveObjects;
	}

	void drawMesh(const std::vector<PrimitiveObject> &primitiveObjects,
				tinygltf::Model &model, tinygltf::Mesh &mesh) {
		
		for (size_t i = 0; i < mesh.primitives.size(); ++i) 
		{
			GLuint vao = primitiveObjects[i].vao;
			std::map<int, GLuint> vbos = primitiveObjects[i].vbos;

			glBindVertexArray(vao);

			tinygltf::Primitive primitive = mesh.primitives[i];
			tinygltf::Accessor indexAccessor = model.accessors[primitive.indices];

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos.at(indexAccessor.bufferView));

			// NEW: Bind the model's texture
			glActiveTexture(GL_TEXTURE0); 
			glBindTexture(GL_TEXTURE_2D, primitiveObjects[i].textureID);
			// Ensure your model shader's sampler is pointed to unit 0
			glUniform1i(glGetUniformLocation(modelShader, "u_BaseColorTexture"), 0);

			glDrawElements(primitive.mode, indexAccessor.count,
						indexAccessor.componentType,
						BUFFER_OFFSET(indexAccessor.byteOffset));

			glBindVertexArray(0);
		}
	}

	void drawModel(const std::vector<PrimitiveObject>& primitiveObjects, tinygltf::Model &model) {
		const tinygltf::Scene &scene = model.scenes[model.defaultScene];
		for (size_t i = 0; i < scene.nodes.size(); ++i) {
			drawModelNodes(primitiveObjects, model, model.nodes[scene.nodes[i]]);
		}
	}

	// Update drawModelNodes to pass the primitiveCounter through
	void drawModelNodes(const std::vector<PrimitiveObject>& primitiveObjects,
						tinygltf::Model &model, tinygltf::Node &node) {
		if ((node.mesh >= 0) && (node.mesh < model.meshes.size())) {
			drawMesh(primitiveObjects, model, model.meshes[node.mesh]);
		}
		for (size_t i = 0; i < node.children.size(); i++) {
			drawModelNodes(primitiveObjects, model, model.nodes[node.children[i]]);
		}
	}

	void update(float deltaTime, float spinSpeed) {
		// 1. Update the dedicated animation variable
		rotationAngle += spinSpeed * deltaTime;

		// 2. Wrap the angle to stay within 0-360 range (optional but good practice)
		if (rotationAngle > 360.0f) rotationAngle -= 360.0f;

		// 3. Update the rotation Y value that the render function uses
		rotation.z = rotationAngle; 
	}

	void render(bool depthPass, glm::mat4 cameraMatrix, glm::mat4 lightSpaceMatrix) {
		// 1. Select the correct shader program
		GLuint activeProgram = depthPass ? depthShader : modelShader;
		glUseProgram(activeProgram);

		// 2. Calculate Model Matrix (Common to both passes)
		glm::mat4 modelMat = glm::mat4(1.0f);
		modelMat = glm::translate(modelMat, position);
		modelMat = glm::rotate(modelMat, rotation.x, glm::vec3(1, 0, 0));
		modelMat = glm::rotate(modelMat, rotation.y, glm::vec3(0, 1, 0));
		modelMat = glm::rotate(modelMat, rotation.z, glm::vec3(0, 0, 1));
		modelMat = glm::scale(modelMat, glm::vec3(scale));

		// 3. Set Uniforms based on the pass
		if (depthPass) {
			// --- PASS 1: SHADOW MAP GENERATION ---
			// Only needs to know where the sun is and where the object is
			glUniformMatrix4fv(glGetUniformLocation(activeProgram, "lightSpaceMatrix"), 1, GL_FALSE, &lightSpaceMatrix[0][0]);
			glUniformMatrix4fv(glGetUniformLocation(activeProgram, "model"), 1, GL_FALSE, &modelMat[0][0]);
		} 
		else {
			// --- PASS 2: MAIN RENDER ---
			// Need standard MVP for the player's camera
			glm::mat4 mvp = cameraMatrix * modelMat;
			glUniformMatrix4fv(glGetUniformLocation(activeProgram, "MVP"), 1, GL_FALSE, &mvp[0][0]);

			// Need these for the shadow calculation inside the fragment shader
			glUniformMatrix4fv(glGetUniformLocation(activeProgram, "lightSpaceMatrix"), 1, GL_FALSE, &lightSpaceMatrix[0][0]);
			glUniformMatrix4fv(glGetUniformLocation(activeProgram, "model"), 1, GL_FALSE, &modelMat[0][0]);
		}

		// 4. Draw the actual geometry
		drawModel(primitiveObjects, model);
	}

	void cleanup() {
		glDeleteProgram(modelShader);
		glDeleteProgram(depthShader);
	}
}; 

// 3d Model specifically for the sun
struct Sun {
	// Shader variable IDs
	GLuint mvpMatrixID;
	GLuint modelShader;
	GLuint depthShader;

	tinygltf::Model model;

	//for position and rotation in render function 
	glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Euler angles
    float scale = 1.0f;

	//for animation
	float rotationAngle = 0.0f;

	// Each VAO corresponds to each mesh primitive in the GLTF model
	struct PrimitiveObject {
		GLuint vao;
		std::map<int, GLuint> vbos;
		GLuint textureID; // Add this to store the texture handle
	};
	std::vector<PrimitiveObject> primitiveObjects;

	bool loadModel(tinygltf::Model &model, const char *filename) {
		tinygltf::TinyGLTF loader;
		std::string err;
		std::string warn;

		bool res = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
		if (!warn.empty()) {
			std::cout << "WARN: " << warn << std::endl;
		}

		if (!err.empty()) {
			std::cout << "ERR: " << err << std::endl;
		}

		if (!res)
			std::cout << "Failed to load glTF: " << filename << std::endl;
		else
			std::cout << "Loaded glTF: " << filename << std::endl;

		return res;
	}

	void initialize(const char* path) {
		// Modify your path if needed

		if (!loadModel(model, path)) {
			return;
		}

		// Prepare buffers for rendering 
		primitiveObjects = bindModel(model);

		// Create and compile our GLSL program from the shaders
		modelShader = LoadShadersFromFile("../final/sun.vert", "../final/sun.frag");
		if (modelShader == 0)
		{
			std::cerr << "Failed to load shaders." << std::endl;
		}

		// Get a handle for GLSL variables
		mvpMatrixID = glGetUniformLocation(modelShader, "MVP");
	}

	void bindMesh(std::vector<PrimitiveObject> &primitiveObjects,
				tinygltf::Model &model, tinygltf::Mesh &mesh) {

		std::map<int, GLuint> vbos;
		for (size_t i = 0; i < model.bufferViews.size(); ++i) {
			const tinygltf::BufferView &bufferView = model.bufferViews[i];

			int target = bufferView.target;
			
			if (bufferView.target == 0) { 
				continue;
			}

			const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
			GLuint vbo;
			glGenBuffers(1, &vbo);
			glBindBuffer(target, vbo);
			glBufferData(target, bufferView.byteLength,
						&buffer.data.at(0) + bufferView.byteOffset, GL_STATIC_DRAW);
			
			vbos[i] = vbo;
		}

		// Each mesh can contain several primitives (or parts), each we need to 
		// bind to an OpenGL vertex array object
		//std::cout << "Mesh primitives: " << mesh.primitives.size() << std::endl;
		for (size_t i = 0; i < mesh.primitives.size(); ++i) {

			tinygltf::Primitive primitive = mesh.primitives[i];
			tinygltf::Accessor indexAccessor = model.accessors[primitive.indices];

			PrimitiveObject primitiveObject;

			GLuint vao;
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);

			for (auto &attrib : primitive.attributes) {
				tinygltf::Accessor accessor = model.accessors[attrib.second];
				int byteStride =
					accessor.ByteStride(model.bufferViews[accessor.bufferView]);
				glBindBuffer(GL_ARRAY_BUFFER, vbos[accessor.bufferView]);

				int size = 1;
				if (accessor.type != TINYGLTF_TYPE_SCALAR) {
					size = accessor.type;
				}

				// TODO: Remember to set the vaa index according to 
				// the buffer in the vertex shader. 
				int vaa = -1;
				std::cout << "Attrib: " << attrib.first << std::endl;
				if (attrib.first.compare("POSITION") == 0) vaa = 0;
				if (attrib.first.compare("TEXCOORD_0") == 0) vaa = 1;
				
				if (vaa > -1) {
					glEnableVertexAttribArray(vaa);
					glVertexAttribPointer(vaa, size, accessor.componentType,
										accessor.normalized ? GL_TRUE : GL_FALSE,
										byteStride, BUFFER_OFFSET(accessor.byteOffset));
				} else {
					std::cout << "vaa missing: " << attrib.first << std::endl;
				}
			}

			// 1. Get the material index from the current primitive (assuming you are in a loop)
			int matIndex = primitive.material; 

			// Only proceed if the primitive actually has a material assigned
			if (matIndex != -1) {
				tinygltf::Material &material = model.materials[matIndex];
				
				// 2. Access the baseColorTexture index from the PBR properties
				int texIndex = material.pbrMetallicRoughness.baseColorTexture.index;

				// 3. Check if the texture exists
				if (texIndex > -1) {
					tinygltf::Texture &tex = model.textures[texIndex];

					if (tex.source > -1) {
						tinygltf::Image &image = model.images[tex.source];

						GLuint texid;
						glGenTextures(1, &texid);
						glBindTexture(GL_TEXTURE_2D, texid);

						primitiveObject.textureID = texid;

						glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

						// 4. Use Sampler settings from the file instead of hardcoded REPEAT
						if (tex.sampler != -1) {
							tinygltf::Sampler &sampler = model.samplers[tex.sampler];
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampler.minFilter != -1 ? sampler.minFilter : GL_LINEAR);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampler.magFilter != -1 ? sampler.magFilter : GL_LINEAR);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sampler.wrapS);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sampler.wrapT);
						} else {
							// Default fallback
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
						}

						GLenum format = GL_RGBA;
						if (image.component == 1) format = GL_RED;
						else if (image.component == 2) format = GL_RG;
						else if (image.component == 3) format = GL_RGB;

						GLenum type = GL_UNSIGNED_BYTE;
						if (image.bits == 16) type = GL_UNSIGNED_SHORT;

						// 5. Use GL_SRGB8_ALPHA8 for baseColor so colors aren't washed out
						glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height, 0, format, type, &image.image.at(0));

						glGenerateMipmap(GL_TEXTURE_2D);
						// Re-enable mipmap filtering if you generated them
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
					}
				}
			}

			// Record VAO for later use
			primitiveObject.vao = vao;
			primitiveObject.vbos = vbos;
			primitiveObjects.push_back(primitiveObject);

			glBindVertexArray(0);
		}
	}

	void bindModelNodes(std::vector<PrimitiveObject> &primitiveObjects, 
						tinygltf::Model &model,
						tinygltf::Node &node) {
		// Bind buffers for the current mesh at the node
		if ((node.mesh >= 0) && (node.mesh < model.meshes.size())) {
			bindMesh(primitiveObjects, model, model.meshes[node.mesh]);
		}

		// Recursive into children nodes
		for (size_t i = 0; i < node.children.size(); i++) {
			assert((node.children[i] >= 0) && (node.children[i] < model.nodes.size()));
			bindModelNodes(primitiveObjects, model, model.nodes[node.children[i]]);
		}
	}

	std::vector<PrimitiveObject> bindModel(tinygltf::Model &model) {
		std::vector<PrimitiveObject> primitiveObjects;

		const tinygltf::Scene &scene = model.scenes[model.defaultScene];
		for (size_t i = 0; i < scene.nodes.size(); ++i) {
			assert((scene.nodes[i] >= 0) && (scene.nodes[i] < model.nodes.size()));
			bindModelNodes(primitiveObjects, model, model.nodes[scene.nodes[i]]);
		}

		return primitiveObjects;
	}

	void drawMesh(const std::vector<PrimitiveObject> &primitiveObjects,
				tinygltf::Model &model, tinygltf::Mesh &mesh) {
		
		for (size_t i = 0; i < mesh.primitives.size(); ++i) 
		{
			GLuint vao = primitiveObjects[i].vao;
			std::map<int, GLuint> vbos = primitiveObjects[i].vbos;

			glBindVertexArray(vao);

			tinygltf::Primitive primitive = mesh.primitives[i];
			tinygltf::Accessor indexAccessor = model.accessors[primitive.indices];

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos.at(indexAccessor.bufferView));

			// NEW: Bind the model's texture
			glActiveTexture(GL_TEXTURE0); 
			glBindTexture(GL_TEXTURE_2D, primitiveObjects[i].textureID);
			// Ensure your model shader's sampler is pointed to unit 0
			glUniform1i(glGetUniformLocation(modelShader, "u_BaseColorTexture"), 0);

			glDrawElements(primitive.mode, indexAccessor.count,
						indexAccessor.componentType,
						BUFFER_OFFSET(indexAccessor.byteOffset));

			glBindVertexArray(0);
		}
	}

	void drawModel(const std::vector<PrimitiveObject>& primitiveObjects, tinygltf::Model &model) {
		const tinygltf::Scene &scene = model.scenes[model.defaultScene];
		for (size_t i = 0; i < scene.nodes.size(); ++i) {
			drawModelNodes(primitiveObjects, model, model.nodes[scene.nodes[i]]);
		}
	}

	// Update drawModelNodes to pass the primitiveCounter through
	void drawModelNodes(const std::vector<PrimitiveObject>& primitiveObjects,
						tinygltf::Model &model, tinygltf::Node &node) {
		if ((node.mesh >= 0) && (node.mesh < model.meshes.size())) {
			drawMesh(primitiveObjects, model, model.meshes[node.mesh]);
		}
		for (size_t i = 0; i < node.children.size(); i++) {
			drawModelNodes(primitiveObjects, model, model.nodes[node.children[i]]);
		}
	}

	void update(float deltaTime, float spinSpeed) {
		// 1. Update the dedicated animation variable
		rotationAngle += spinSpeed * deltaTime;

		// 2. Wrap the angle to stay within 0-360 range (optional but good practice)
		if (rotationAngle > 360.0f) rotationAngle -= 360.0f;

		// 3. Update the rotation Y value that the render function uses
		rotation.z = rotationAngle; 
	}

	void render(glm::mat4 viewMatrix, glm::mat4 projectionMatrix) {
		// 1. Select the correct shader program
		GLuint activeProgram = modelShader;
		glUseProgram(activeProgram);

		// 2. Calculate Model Matrix (Common to both passes)
		glm::mat4 modelMat = glm::mat4(1.0f);
		modelMat = glm::translate(modelMat, position);
		modelMat = glm::rotate(modelMat, rotation.x, glm::vec3(1, 0, 0));
		modelMat = glm::rotate(modelMat, rotation.y, glm::vec3(0, 1, 0));
		modelMat = glm::rotate(modelMat, rotation.z, glm::vec3(0, 0, 1));
		modelMat = glm::scale(modelMat, glm::vec3(scale));

		glm::mat4 viewRotationOnly = glm::mat4(glm::mat3(viewMatrix));

		glm::mat4 mvp = projectionMatrix * viewRotationOnly * modelMat;
		glUniformMatrix4fv(glGetUniformLocation(activeProgram, "MVP"), 1, GL_FALSE, &mvp[0][0]);

		// 4. Draw the actual geometry
		drawModel(primitiveObjects, model);
	}

	void cleanup() {
		glDeleteProgram(modelShader);
		glDeleteProgram(depthShader);
	}
}; 

// floor grid 
struct Floor {
    glm::vec3 position;
    glm::vec3 scale;

    // 4 Vertices for a flat plane on the XZ axis
    GLfloat vertex_buffer_data[12] = {
        -1.0f, 0.0f,  1.0f,  // Front Left
         1.0f, 0.0f,  1.0f,  // Front Right
         1.0f, 0.0f, -1.0f,  // Back Right
        -1.0f, 0.0f, -1.0f   // Back Left
    };

    // Simple white color buffer (color logic is mostly handled in the shader)
    GLfloat color_buffer_data[12] = {
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f
    };

    // UVs from 0 to 1 across the whole plane
    GLfloat uv_buffer_data[8] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };

    GLuint index_buffer_data[6] = {
        0, 1, 2,
        0, 2, 3
    };

    // OpenGL IDs
    GLuint vertexArrayID, vertexBufferID, indexBufferID, colorBufferID, uvBufferID;
    GLuint programID, mvpMatrixID;
    
    // Grid-specific Uniform IDs
    GLuint gridColorID, floorColorID, gridScaleID;

    void initialize(glm::vec3 position, glm::vec3 scale) {
        this->position = position;
        this->scale = scale;

        glGenVertexArrays(1, &vertexArrayID);
        glBindVertexArray(vertexArrayID);

        // Position Buffer
        glGenBuffers(1, &vertexBufferID);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBufferID);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_buffer_data), vertex_buffer_data, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0); // <--- ADD THIS
    	glEnableVertexAttribArray(0);

        // Color Buffer
        glGenBuffers(1, &colorBufferID);
        glBindBuffer(GL_ARRAY_BUFFER, colorBufferID);
        glBufferData(GL_ARRAY_BUFFER, sizeof(color_buffer_data), color_buffer_data, GL_STATIC_DRAW);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (void*)0); // <--- ADD THIS
    	glEnableVertexAttribArray(1);
        
		// UV Buffer
        glGenBuffers(1, &uvBufferID);
        glBindBuffer(GL_ARRAY_BUFFER, uvBufferID);
        glBufferData(GL_ARRAY_BUFFER, sizeof(uv_buffer_data), uv_buffer_data, GL_STATIC_DRAW);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)0); // <--- ADD THIS
    	glEnableVertexAttribArray(2);

        // Index Buffer
        glGenBuffers(1, &indexBufferID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_buffer_data), index_buffer_data, GL_STATIC_DRAW);

        // Load Shaders 
        programID = LoadShadersFromFile("../final/floor.vert", "../final/floor.frag");
		if (programID == 0)
		{
			std::cerr << "Failed to load shaders." << std::endl;
		}
        
        // Get Uniform Handles
        mvpMatrixID  = glGetUniformLocation(programID, "MVP");
        gridColorID  = glGetUniformLocation(programID, "gridColor");
        floorColorID = glGetUniformLocation(programID, "floorColor");
        gridScaleID  = glGetUniformLocation(programID, "gridScale");

		glBindVertexArray(0); // Unbind VAO to stay clean
    }

    void render(glm::mat4 cameraMatrix, glm::mat4 lightSpaceMatrix, glm::vec3 playerPosition, glm::vec3 gColor, glm::vec3 fColor, float gScale) {
		glUseProgram(programID);
		glBindVertexArray(vertexArrayID);

		// 4. Transform Matrices
		glm::mat4 modelMatrix = glm::mat4(1.0f);
		modelMatrix = glm::translate(modelMatrix, position);
		modelMatrix = glm::scale(modelMatrix, scale);
		
		// Calculate MVP for the player camera
		glm::mat4 mvp = cameraMatrix * modelMatrix;
		
		glUniformMatrix4fv(mvpMatrixID, 1, GL_FALSE, &mvp[0][0]);
		glUniformMatrix4fv(glGetUniformLocation(programID, "model"), 1, GL_FALSE, &modelMatrix[0][0]);
		glUniformMatrix4fv(glGetUniformLocation(programID, "lightSpaceMatrix"), 1, GL_FALSE, &lightSpaceMatrix[0][0]);
		glUniform3fv(glGetUniformLocation(programID, "playerPos"), 1, &playerPosition[0]);

		// 5. Update Grid Colors and Scale
		glUniform3fv(gridColorID, 1, &gColor[0]);
		glUniform3fv(floorColorID, 1, &fColor[0]);
		glUniform1f(gridScaleID, gScale);

		// 6. Bind the Shadow Map texture (Pass 1 result)
		// Assuming your shadow map is on Texture Unit 1
		glUniform1i(glGetUniformLocation(programID, "shadowMap"), 1);

		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)0);
		glBindVertexArray(0);
	}

    void cleanup() {
        glDeleteBuffers(1, &vertexBufferID);
        glDeleteBuffers(1, &colorBufferID);
        glDeleteBuffers(1, &uvBufferID);
        glDeleteBuffers(1, &indexBufferID);
        glDeleteVertexArrays(1, &vertexArrayID);
        glDeleteProgram(programID);
    }
};

// bg 	
struct Background {
    unsigned int VAO, VBO;
    unsigned int programID;
    
    // Uniform Handles
    unsigned int colorTopID;
    unsigned int colorBottomID;

    void init() {
        // Setup Quad Geometry (Screen-space)
        float vertices[] = {
            -1.0f,  1.0f, 0.0f, // Top Left
            -1.0f, -1.0f, 0.0f, // Bottom Left
             1.0f,  1.0f, 0.0f, // Top Right
             1.0f, -1.0f, 0.0f  // Bottom Right
        };

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

		// Load Shaders from File
        programID = LoadShadersFromFile("../final/bg.vert", "../final/bg.frag");
        
        if (programID == 0) {
            std::cerr << "Failed to load background shaders." << std::endl;
            return;
        }

        // Get Uniform Handles
        colorTopID    = glGetUniformLocation(programID, "colorTop");
        colorBottomID = glGetUniformLocation(programID, "colorBottom");

        glBindVertexArray(0); 
    }

    void draw(const glm::vec3& topColor, const glm::vec3& bottomColor) {
        glDisable(GL_DEPTH_TEST); 
        glDepthMask(GL_FALSE); 

        glUseProgram(programID);

        // glUniform3fv is used to pass a vec3 (pointer to 3 floats)
        glUniform3fv(colorTopID, 1, &topColor[0]);
        glUniform3fv(colorBottomID, 1, &bottomColor[0]);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    void cleanup() {
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteProgram(programID);
    }
};

// bloom framebuffer
static void bloomFBOinit(int SCR_WIDTH, int SCR_HEIGHT) {
	glGenFramebuffers(1, &hdrFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);

	glGenTextures(1, &colorBuffer);
	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	// GL_RGBA16F is the key for HDR (High Dynamic Range)
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer, 0);

	// Depth Buffer so 3D models still render correctly
	GLuint rboDepth;
	glGenRenderbuffers(1, &rboDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepth);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Framebuffer not complete!" << std::endl;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void initBlurFBOs(int width, int height) {
    glGenFramebuffers(2, pingpongFBO);
    glGenTextures(2, pingpongColorbuffers);
    for (unsigned int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[i]);
        glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // Prevents blur bleeding
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingpongColorbuffers[i], 0);
    }
}

struct ScreenQuad {
    // 4 Vertices for a full-screen quad (NDC coordinates)
    GLfloat vertex_buffer_data[12] = {
        -1.0f,  1.0f, 0.0f,  // Top Left
        -1.0f, -1.0f, 0.0f,  // Bottom Left
         1.0f, -1.0f, 0.0f,  // Bottom Right
         1.0f,  1.0f, 0.0f   // Top Right
    };

    GLfloat uv_buffer_data[8] = {
        0.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f
    };

    GLuint index_buffer_data[6] = {
        0, 1, 2,
        0, 2, 3
    };

    GLuint vertexArrayID, vertexBufferID, indexBufferID, uvBufferID;
    GLuint programID, sceneTexID, bloomTexID, exposureID;

    void initialize() {
        glGenVertexArrays(1, &vertexArrayID);
        glBindVertexArray(vertexArrayID);

        // Position Buffer
        glGenBuffers(1, &vertexBufferID);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBufferID);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_buffer_data), vertex_buffer_data, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(0);

        // UV Buffer
        glGenBuffers(1, &uvBufferID);
        glBindBuffer(GL_ARRAY_BUFFER, uvBufferID);
        glBufferData(GL_ARRAY_BUFFER, sizeof(uv_buffer_data), uv_buffer_data, GL_STATIC_DRAW);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(1);

        // Index Buffer
        glGenBuffers(1, &indexBufferID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_buffer_data), index_buffer_data, GL_STATIC_DRAW);

        // Load Shaders
        programID = LoadShadersFromFile("../final/bloom.vert", "../final/bloom.frag");
        
		if (programID == 0) {
			std::cerr << "Failed to load screen quad shaders." << std::endl;
			return;
		}

        // Get Uniform Handles
        sceneTexID  = glGetUniformLocation(programID, "scene");
        bloomTexID  = glGetUniformLocation(programID, "bloomBlur");
        exposureID  = glGetUniformLocation(programID, "exposure");
    }

    void render(GLuint originalSceneTex, GLuint blurredGlowTex, float exposure) {
        glUseProgram(programID);
        glBindVertexArray(vertexArrayID);

        // Texture Unit 0: The crisp scene
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, originalSceneTex);
        glUniform1i(sceneTexID, 0);

        // Texture Unit 1: The blurred glow
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, blurredGlowTex);
        glUniform1i(bloomTexID, 1);

        glUniform1f(exposureID, exposure);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)0);
        glBindVertexArray(0);
    }

	void draw() {
		glBindVertexArray(vertexArrayID);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)0);
		glBindVertexArray(0);
	}

	void cleanup() {
		glDeleteBuffers(1, &vertexBufferID);
		glDeleteBuffers(1, &uvBufferID);
		glDeleteBuffers(1, &indexBufferID);
		glDeleteVertexArrays(1, &vertexArrayID);
		glDeleteProgram(programID);
	}
};

// skeletal animation model
struct MyBot {
	// Shader variable IDs
	GLuint mvpMatrixID;
	GLuint jointMatricesID;
	GLuint lightPositionID;
	GLuint lightIntensityID;
	GLuint programID;

	tinygltf::Model model;

	std::vector<glm::mat4> m_globalTransforms;

	// Each VAO corresponds to each mesh primitive in the GLTF model
	struct PrimitiveObject {
		GLuint vao;
		std::map<int, GLuint> vbos;
	};
	std::vector<PrimitiveObject> primitiveObjects;

	// Skinning 
	struct SkinObject {
		// Transforms the geometry into the space of the respective joint
		std::vector<glm::mat4> inverseBindMatrices;  

		// Transforms the geometry following the movement of the joints
		std::vector<glm::mat4> globalJointTransforms;

		// Combined transforms
		std::vector<glm::mat4> jointMatrices;
	};
	std::vector<SkinObject> skinObjects;

	// Animation 
	struct SamplerObject {
		std::vector<float> input;
		std::vector<glm::vec4> output;
		int interpolation;
	};
	struct ChannelObject {
		int sampler;
		std::string targetPath;
		int targetNode;
	}; 
	struct AnimationObject {
		std::vector<SamplerObject> samplers;	// Animation data
	};
	std::vector<AnimationObject> animationObjects;

	glm::mat4 getNodeTransform(const tinygltf::Node& node) {
		glm::mat4 transform(1.0f); 

		if (node.matrix.size() == 16) {
			transform = glm::make_mat4(node.matrix.data());
		} else {
			if (node.translation.size() == 3) {
				transform = glm::translate(transform, glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
			}
			if (node.rotation.size() == 4) {
				glm::quat q(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
				transform *= glm::mat4_cast(q);
			}
			if (node.scale.size() == 3) {
				transform = glm::scale(transform, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
			}
		}
		return transform;
	}

	void computeLocalNodeTransform(const tinygltf::Model& model, 
		int nodeIndex, 
		std::vector<glm::mat4> &localTransforms)
	{
		glm::mat4 nodeTransform; 
		nodeTransform = getNodeTransform(model.nodes[nodeIndex]);
		localTransforms[nodeIndex] = nodeTransform;

		const tinygltf::Node& node = model.nodes[nodeIndex];
		for (const int childIndex : node.children) {
			computeLocalNodeTransform(model, childIndex, localTransforms);
		}
	}

	void computeGlobalNodeTransform(const tinygltf::Model& model, 
		const std::vector<glm::mat4> &localTransforms,
		int nodeIndex, const glm::mat4& parentTransform, 
		std::vector<glm::mat4> &globalTransforms)
	{
		glm::mat4 globalTransform = parentTransform * localTransforms[nodeIndex];
		globalTransforms[nodeIndex] = globalTransform;

		const tinygltf::Node& node = model.nodes[nodeIndex];
		for (const int childIndex : node.children) {
			computeGlobalNodeTransform(model, localTransforms, childIndex, globalTransform, globalTransforms);
		}

	}

	std::vector<SkinObject> prepareSkinning(const tinygltf::Model &model) {
		std::vector<SkinObject> skinObjects;

		for (size_t i = 0; i < model.skins.size(); i++) {
			SkinObject skinObject;
			const tinygltf::Skin &skin = model.skins[i];

			// 1. Load Inverse Bind Matrices
			const tinygltf::Accessor &accessor = model.accessors[skin.inverseBindMatrices];
			const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
			const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
			const float *ptr = reinterpret_cast<const float *>(
				buffer.data.data() + accessor.byteOffset + bufferView.byteOffset);
			
			skinObject.inverseBindMatrices.resize(accessor.count);
			for (size_t j = 0; j < accessor.count; j++) {
				skinObject.inverseBindMatrices[j] = glm::make_mat4(ptr + j * 16);
			}

			// 2. Initialize Global Transforms for all nodes in the model
			std::vector<glm::mat4> localTransforms(model.nodes.size(), glm::mat4(1.0f));
			std::vector<glm::mat4> globalTransforms(model.nodes.size(), glm::mat4(1.0f));

			// fill localTransforms
			const tinygltf::Scene &scene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
			for (int rootIndex : scene.nodes) {
				computeLocalNodeTransform(model, rootIndex, localTransforms);
			}

			// compute globalTransforms
			for (int rootIndex : scene.nodes) {
				computeGlobalNodeTransform(model, localTransforms, rootIndex, glm::mat4(1.0f), globalTransforms);
			}

			// 3. Compute joint matrices
			skinObject.jointMatrices.resize(skin.joints.size());
			for (size_t j = 0; j < skin.joints.size(); ++j) {
				int jointNodeIndex = skin.joints[j];

				// Use the global transform we just calculated for this specific joint node
				glm::mat4 globalTransform = globalTransforms[jointNodeIndex];
				glm::mat4 inverseBindMatrix = skinObject.inverseBindMatrices[j];

				// The core skinning equation
				skinObject.jointMatrices[j] = globalTransform * inverseBindMatrix;
			}

			skinObjects.push_back(skinObject);
		}
		return skinObjects;
	}

	int findKeyframeIndex(const std::vector<float>& times, float animationTime) 
	{
		int left = 0;
		int right = times.size() - 1;

		while (left <= right) {
			int mid = (left + right) / 2;

			if (mid + 1 < times.size() && times[mid] <= animationTime && animationTime < times[mid + 1]) {
				return mid;
			}
			else if (times[mid] > animationTime) {
				right = mid - 1;
			}
			else { // animationTime >= times[mid + 1]
				left = mid + 1;
			}
		}

		// Target not found
		return (times.size() < 2) ? 0 : times.size() - 2;
	}

	std::vector<AnimationObject> prepareAnimation(const tinygltf::Model &model) 
	{
		std::vector<AnimationObject> animationObjects;
		for (const auto &anim : model.animations) {
			AnimationObject animationObject;
			
			for (const auto &sampler : anim.samplers) {
				SamplerObject samplerObject;

				const tinygltf::Accessor &inputAccessor = model.accessors[sampler.input];
				const tinygltf::BufferView &inputBufferView = model.bufferViews[inputAccessor.bufferView];
				const tinygltf::Buffer &inputBuffer = model.buffers[inputBufferView.buffer];

				assert(inputAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
				assert(inputAccessor.type == TINYGLTF_TYPE_SCALAR);

				// Input (time) values
				samplerObject.input.resize(inputAccessor.count);

				const unsigned char *inputPtr = &inputBuffer.data[inputBufferView.byteOffset + inputAccessor.byteOffset];
				const float *inputBuf = reinterpret_cast<const float*>(inputPtr);

				// Read input (time) values
				int stride = inputAccessor.ByteStride(inputBufferView);
				for (size_t i = 0; i < inputAccessor.count; ++i) {
					samplerObject.input[i] = *reinterpret_cast<const float*>(inputPtr + i * stride);
				}
				
				const tinygltf::Accessor &outputAccessor = model.accessors[sampler.output];
				const tinygltf::BufferView &outputBufferView = model.bufferViews[outputAccessor.bufferView];
				const tinygltf::Buffer &outputBuffer = model.buffers[outputBufferView.buffer];

				assert(outputAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

				const unsigned char *outputPtr = &outputBuffer.data[outputBufferView.byteOffset + outputAccessor.byteOffset];
				const float *outputBuf = reinterpret_cast<const float*>(outputPtr);

				int outputStride = outputAccessor.ByteStride(outputBufferView);
				
				// Output values
				samplerObject.output.resize(outputAccessor.count);
				
				for (size_t i = 0; i < outputAccessor.count; ++i) {
					// Calculate exact pointer using stride
					const unsigned char* elementPtr = outputPtr + (i * outputStride);

					if (outputAccessor.type == TINYGLTF_TYPE_VEC3) {
						glm::vec3 val;
						memcpy(&val, elementPtr, 3 * sizeof(float));
						samplerObject.output[i] = glm::vec4(val, 0.0f); // Pad with 0
					} else if (outputAccessor.type == TINYGLTF_TYPE_VEC4) {
						memcpy(&samplerObject.output[i], elementPtr, 4 * sizeof(float));
					}
				}

				animationObject.samplers.push_back(samplerObject);			
			}

			animationObjects.push_back(animationObject);
		}
		return animationObjects;
	}

	void updateAnimationValues(
		const tinygltf::Model &model, 
		const tinygltf::Animation &anim, 
		const AnimationObject &animationObject, 
		float time,
		std::vector<glm::vec3> &translations,
    	std::vector<glm::quat> &rotations,
    	std::vector<glm::vec3> &scales) 
	{
		// There are many channels so we have to accumulate the transforms 
		for (const auto &channel : anim.channels) {
			
			int targetNodeIndex = channel.target_node;
			const auto &sampler = anim.samplers[channel.sampler];
			
			// Access output (value) data for the channel
			const tinygltf::Accessor &outputAccessor = model.accessors[sampler.output];
			const tinygltf::BufferView &outputBufferView = model.bufferViews[outputAccessor.bufferView];
			const tinygltf::Buffer &outputBuffer = model.buffers[outputBufferView.buffer];

			// Calculate current animation time (wrap if necessary)
			const std::vector<float> &times = animationObject.samplers[channel.sampler].input;
			if (times.empty()) continue;
			float animationTime = fmod(time, times.back());
			
			// ----------------------------------------------------------
			// TODO: Find a keyframe for getting animation data 
			// ----------------------------------------------------------
			int keyframeIndex = findKeyframeIndex(times, animationTime); 

			int nextKeyframeIndex = (keyframeIndex + 1) % times.size();

			float t = 0.0f;
			float timeStart = times[keyframeIndex];
			float timeEnd = times[nextKeyframeIndex];
			
			// Check for validity to avoid division by zero
			if (timeEnd > timeStart) {
				t = (animationTime - timeStart) / (timeEnd - timeStart);
			} else if (nextKeyframeIndex == 0) {
				// Handle looping (end of animation -> start)
				// The duration of this last segment is (MaxTime - timeStart) + timeEnd
				float segmentDuration = (times.back() - timeStart) + timeEnd;
				float timeIntoSegment = (animationTime - timeStart);
				if(timeIntoSegment < 0) timeIntoSegment += times.back(); // Wrapped around
				
				// Simplified: If looping is complex, just clamping t=0 is often safe enough for basic needs
				t = 0.0f; 
			}

			const unsigned char *outputPtr = &outputBuffer.data[outputBufferView.byteOffset + outputAccessor.byteOffset];
			const float *outputBuf = reinterpret_cast<const float*>(outputPtr);

			// --- UPDATED ASSIGNMENT LOGIC ---
			if (channel.target_path == "translation") {
				glm::vec3 val0 = glm::make_vec3(outputBuf + keyframeIndex * 3);
				glm::vec3 val1 = glm::make_vec3(outputBuf + nextKeyframeIndex * 3);
				translations[targetNodeIndex] = glm::mix(val0, val1, t); // OVERRIDE
			} 
			else if (channel.target_path == "rotation") {
				float raw0[4], raw1[4];
				memcpy(raw0, outputBuf + keyframeIndex * 4, 4 * sizeof(float));
				memcpy(raw1, outputBuf + nextKeyframeIndex * 4, 4 * sizeof(float));
				
				// glTF is (x,y,z,w), GLM constructor is (w,x,y,z)
				glm::quat q0(raw0[3], raw0[0], raw0[1], raw0[2]); 
				glm::quat q1(raw1[3], raw1[0], raw1[1], raw1[2]);
				
				rotations[targetNodeIndex] = glm::normalize(glm::slerp(q0, q1, t)); // OVERRIDE
			} 
			else if (channel.target_path == "scale") {
				glm::vec3 val0 = glm::make_vec3(outputBuf + keyframeIndex * 3);
				glm::vec3 val1 = glm::make_vec3(outputBuf + nextKeyframeIndex * 3);
				scales[targetNodeIndex] = glm::mix(val0, val1, t); // OVERRIDE
			}
		}
	}

	void updateSkinning(const std::vector<glm::mat4> &globalTransforms) {
    for (size_t k = 0; k < model.skins.size(); k++) {
        const tinygltf::Skin &skin = model.skins[k];
        SkinObject &skinObject = skinObjects[k];

        // 1. IMPORTANT: Find the node that actually uses this skin.
        // Usually, you can find which node has 'node.skin == k'.
        glm::mat4 invMeshGlobalTransform = glm::mat4(1.0f);
        for (size_t n = 0; n < model.nodes.size(); ++n) {
            if (model.nodes[n].skin == (int)k) {
                invMeshGlobalTransform = glm::inverse(globalTransforms[n]);
                break;
            }
        }

        for (size_t i = 0; i < skin.joints.size(); ++i) {
            int jointNodeIndex = skin.joints[i];
            const glm::mat4& globalBoneTransform = globalTransforms[jointNodeIndex];
            const glm::mat4& inverseBindMatrix = skinObject.inverseBindMatrices[i];

            // 2. The formula that fixes the "Massive Scale" and "Offset" issues:
            skinObject.jointMatrices[i] = invMeshGlobalTransform * globalBoneTransform * inverseBindMatrix;
        }
    }
}

	// Helper to build matrix from TRS
	glm::mat4 composeTransform(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
		glm::mat4 mat = glm::translate(glm::mat4(1.0f), t);
		mat = mat * glm::mat4_cast(r);
		mat = glm::scale(mat, s);
		return mat;
	}

	void update(float time) {
		if (model.animations.size() > 0 && !model.skins.empty()) {
			const tinygltf::Animation &animation = model.animations[0];
			const AnimationObject &animationObject = animationObjects[0];
			
			// 1. Initialize TRS components for ALL nodes from their bind pose
			std::vector<glm::vec3> translations(model.nodes.size());
			std::vector<glm::quat> rotations(model.nodes.size());
			std::vector<glm::vec3> scales(model.nodes.size());

			for(size_t i = 0; i < model.nodes.size(); ++i) {
				const tinygltf::Node& node = model.nodes[i];
				
				// Default values
				translations[i] = glm::vec3(0.0f);
				rotations[i] = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity (w,x,y,z)
				scales[i] = glm::vec3(1.0f);

				// Load from glTF if present
				if (node.translation.size() == 3) 
					translations[i] = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
				if (node.rotation.size() == 4) 
					rotations[i] = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
				if (node.scale.size() == 3) 
					scales[i] = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
			}

			// 2. Apply Animation (Override specific components)
			// Pass the vectors by reference to be modified
			updateAnimationValues(model, animation, animationObject, time, translations, rotations, scales);

			// 3. Rebuild the nodeTransforms matrix from the updated components
			std::vector<glm::mat4> nodeTransforms(model.nodes.size());
			for(size_t i = 0; i < model.nodes.size(); ++i) {
				nodeTransforms[i] = composeTransform(translations[i], rotations[i], scales[i]);
			}

			// 4. Calculate Global Transforms (Standard recursion)
			std::vector<glm::mat4> globalTransforms(model.nodes.size(), glm::mat4(1.0f));
			const tinygltf::Scene &scene = model.scenes[model.defaultScene];
			for (int rootNodeIdx : scene.nodes) {
				computeGlobalNodeTransform(model, nodeTransforms, rootNodeIdx, glm::mat4(1.0f), globalTransforms);
			}

			// 5. Skinning
			updateSkinning(globalTransforms);
			this->m_globalTransforms = globalTransforms;
		}
	}

	bool loadModel(tinygltf::Model &model, const char *filename) {
		tinygltf::TinyGLTF loader;
		std::string err;
		std::string warn;

		bool res = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
		if (!warn.empty()) {
			std::cout << "WARN: " << warn << std::endl;
		}

		if (!err.empty()) {
			std::cout << "ERR: " << err << std::endl;
		}

		if (!res)
			std::cout << "Failed to load glTF: " << filename << std::endl;
		else
			std::cout << "Loaded glTF: " << filename << std::endl;

		return res;
	}

	void initialize() {
		// Modify your path if needed
		if (!loadModel(model, "../final/model/walking_astronaut/scene.gltf")) {
			return;
		}

		// Prepare buffers for rendering 
		primitiveObjects = bindModel(model);

		// Prepare joint matrices
		skinObjects = prepareSkinning(model);

		// Prepare animation data 
		animationObjects = prepareAnimation(model);

		// Create and compile our GLSL program from the shaders
		programID = LoadShadersFromFile("../final/bot.vert", "../final/bot.frag");
		if (programID == 0)
		{
			std::cerr << "Failed to load shaders." << std::endl;
		}

		// Get a handle for GLSL variables
		mvpMatrixID = glGetUniformLocation(programID, "MVP");
		lightPositionID = glGetUniformLocation(programID, "lightPosition");
		lightIntensityID = glGetUniformLocation(programID, "lightIntensity");
		jointMatricesID = glGetUniformLocation(programID, "jointMatrices");
		
		// Check if it was found (Optional but recommended)
		if (jointMatricesID == -1) {
			std::cerr << "Warning: jointMatrices uniform not found in shader!" << std::endl;
		}
	}

	void bindMesh(std::vector<PrimitiveObject> &primitiveObjects,
				tinygltf::Model &model, tinygltf::Mesh &mesh) {

		std::map<int, GLuint> vbos;
		for (size_t i = 0; i < model.bufferViews.size(); ++i) {
			const tinygltf::BufferView &bufferView = model.bufferViews[i];
			
			int target = bufferView.target;
			
			// FIX: If target is 0 (common for skinning), treat it as GL_ARRAY_BUFFER
			if (target == 0) { 
				target = GL_ARRAY_BUFFER; 
			}

			const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
			GLuint vbo;
			glGenBuffers(1, &vbo);
			glBindBuffer(target, vbo);
			glBufferData(target, bufferView.byteLength,
						&buffer.data.at(0) + bufferView.byteOffset, GL_STATIC_DRAW);
			
			vbos[i] = vbo;
		}

		// Each mesh can contain several primitives (or parts), each we need to 
		// bind to an OpenGL vertex array object
		for (size_t i = 0; i < mesh.primitives.size(); ++i) {

			tinygltf::Primitive primitive = mesh.primitives[i];
			tinygltf::Accessor indexAccessor = model.accessors[primitive.indices];

			GLuint vao;
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);

			for (auto &attrib : primitive.attributes) {
				tinygltf::Accessor accessor = model.accessors[attrib.second];
				int byteStride =
					accessor.ByteStride(model.bufferViews[accessor.bufferView]);
				glBindBuffer(GL_ARRAY_BUFFER, vbos[accessor.bufferView]);

				int size = 1;
				if (accessor.type != TINYGLTF_TYPE_SCALAR) {
					size = accessor.type;
				}

				int vaa = -1;
				// Map strings to your specific attribute locations
				if (attrib.first.compare("POSITION") == 0)   vaa = 0;
				else if (attrib.first.compare("NORMAL") == 0)     vaa = 1;
				else if (attrib.first.compare("TEXCOORD_0") == 0) vaa = 2;
				// else if (attrib.first.compare("TEXCOORD_1") == 0) vaa = 4;
				// else if (attrib.first.compare("TEXCOORD_2") == 0) vaa = 5;
				// else if (attrib.first.compare("TEXCOORD_3") == 0) vaa = 6;
				// else if (attrib.first.compare("TEXCOORD_4") == 0) vaa = 7;
				else if (attrib.first.compare("JOINTS_0") == 0)   vaa = 3;
				else if (attrib.first.compare("WEIGHTS_0") == 0)  vaa = 4;
				else if (attrib.first.compare("TANGENT") == 0)    vaa = 5;
				if (vaa > -1) {
					glEnableVertexAttribArray(vaa);
					// Use glVertexAttribIPointer for JOINTS (integers) to avoid casting to float
					if (attrib.first.compare("JOINTS_0") == 0) {
						glVertexAttribIPointer(vaa, size, accessor.componentType, 
											byteStride, BUFFER_OFFSET(accessor.byteOffset));
					} else {
						glVertexAttribPointer(vaa, size, accessor.componentType,
											accessor.normalized ? GL_TRUE : GL_FALSE,
											byteStride, BUFFER_OFFSET(accessor.byteOffset));
					}
				} else {
					std::cout << "vaa missing: " << attrib.first << std::endl;
				}
			}

			// Record VAO for later use
			PrimitiveObject primitiveObject;
			primitiveObject.vao = vao;
			primitiveObject.vbos = vbos;
			primitiveObjects.push_back(primitiveObject);

			glBindVertexArray(0);
		}
	}

	void bindModelNodes(std::vector<PrimitiveObject> &primitiveObjects, 
						tinygltf::Model &model,
						tinygltf::Node &node) {
		// Bind buffers for the current mesh at the node
		if ((node.mesh >= 0) && (node.mesh < model.meshes.size())) {
			bindMesh(primitiveObjects, model, model.meshes[node.mesh]);
		}

		// Recursive into children nodes
		for (size_t i = 0; i < node.children.size(); i++) {
			assert((node.children[i] >= 0) && (node.children[i] < model.nodes.size()));
			bindModelNodes(primitiveObjects, model, model.nodes[node.children[i]]);
		}
	}

	std::vector<PrimitiveObject> bindModel(tinygltf::Model &model) {
		std::vector<PrimitiveObject> primitiveObjects;

		const tinygltf::Scene &scene = model.scenes[model.defaultScene];
		for (size_t i = 0; i < scene.nodes.size(); ++i) {
			assert((scene.nodes[i] >= 0) && (scene.nodes[i] < model.nodes.size()));
			bindModelNodes(primitiveObjects, model, model.nodes[scene.nodes[i]]);
		}

		return primitiveObjects;
	}

	// 1. Updated drawMesh: Added int &primitiveIdx
	void drawMesh(const std::vector<PrimitiveObject> &primitiveObjects,
				tinygltf::Model &model, tinygltf::Mesh &mesh, int &primitiveIdx) {
		
		for (size_t i = 0; i < mesh.primitives.size(); ++i) 
		{
			// Check bounds to be safe
			if (primitiveIdx >= primitiveObjects.size()) {
				std::cerr << "Error: primitiveIdx out of bounds!" << std::endl;
				return;
			}

			// Access using the global index, then increment it
			GLuint vao = primitiveObjects[primitiveIdx].vao;
			std::map<int, GLuint> vbos = primitiveObjects[primitiveIdx].vbos;
			primitiveIdx++; 

			glBindVertexArray(vao);

			tinygltf::Primitive primitive = mesh.primitives[i];
			tinygltf::Accessor indexAccessor = model.accessors[primitive.indices];

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos.at(indexAccessor.bufferView));

			glDrawElements(primitive.mode, indexAccessor.count,
							indexAccessor.componentType,
							BUFFER_OFFSET(indexAccessor.byteOffset));

			glBindVertexArray(0);
		}
	}

	void drawModelNodes(const std::vector<PrimitiveObject>& primitiveObjects,
                    tinygltf::Model &model, tinygltf::Node &node, 
                    int &primitiveIdx, 
                    const glm::mat4& viewProjection, 
                    const std::vector<glm::mat4>& globalTransforms) {
    
		if ((node.mesh >= 0) && (node.mesh < model.meshes.size())) {
        
			int nodeIndex = (int)(&node - &model.nodes[0]); 
			glm::mat4 modelMatrix = globalTransforms[nodeIndex];
			
			// --- CRITICAL FIX: Handle Skinned vs Static Meshes ---
			glm::mat4 finalModelMatrix;
			
			// If this node is skinned, the bones handle the position/scale.
			// We must set the Model Matrix to Identity to prevent "Double Transforms".
			if (node.skin >= 0) {
				finalModelMatrix = glm::mat4(1.0f);
			} else {
				// Static objects (like the backpack or accessories) need their transform
				finalModelMatrix = modelMatrix;
			}

			// Calculate MVP using the corrected matrix
			glm::mat4 mvp = viewProjection * finalModelMatrix;

			// Send to Shader
			glUniformMatrix4fv(mvpMatrixID, 1, GL_FALSE, &mvp[0][0]);
			
			// Also fix u_model (used for normals/lighting)
			GLint u_modelLoc = glGetUniformLocation(programID, "u_model"); // (Move this to initialize() for performance!)
			glUniformMatrix4fv(u_modelLoc, 1, GL_FALSE, &finalModelMatrix[0][0]);

			drawMesh(primitiveObjects, model, model.meshes[node.mesh], primitiveIdx);
		}

		// 2. Recurse into children
		for (size_t i = 0; i < node.children.size(); i++) {
			drawModelNodes(primitiveObjects, model, model.nodes[node.children[i]], 
						primitiveIdx, viewProjection, globalTransforms);
		}
	}

	void render(glm::mat4 cameraMatrix) {
		glUseProgram(programID);

		// 1. Create a Global Model Matrix to scale/position the entire astronaut
		glm::mat4 globalModelTransform = glm::mat4(1.0f);
		globalModelTransform = glm::translate(globalModelTransform, glm::vec3(0.0f, -25.0f, -20.0f)); // Position him
		globalModelTransform = glm::scale(globalModelTransform, glm::vec3(0.08f)); // Scale him down (e.g., 0.1x)

		// 2. Combine with Camera
		glm::mat4 scaledViewProjection = cameraMatrix * globalModelTransform;
		
		// 1. Upload Light Data
		glUniform3fv(lightPositionID, 1, &lightPosition[0]);
		glUniform3fv(lightIntensityID, 1, &lightIntensity[0]);

		// 2. Upload ALL joint matrices for the first skin
		if (!skinObjects.empty()) {
			glUniformMatrix4fv(jointMatricesID, 
							(GLsizei)skinObjects[0].jointMatrices.size(), 
							GL_FALSE, 
							&skinObjects[0].jointMatrices[0][0][0]);
		}

		// 3. Draw with Node Transforms
		int primitiveIdx = 0;
		const tinygltf::Scene &scene = model.scenes[model.defaultScene];
		for (int rootNodeIdx : scene.nodes) {
			drawModelNodes(primitiveObjects, model, model.nodes[rootNodeIdx], 
						primitiveIdx, scaledViewProjection, m_globalTransforms);
		}
	}

	void cleanup() {
		glDeleteProgram(programID);
	}
}; 

int main(void)
{
	// Initialise GLFW
	if (!glfwInit())
	{
		std::cerr << "Failed to initialize GLFW." << std::endl;
		return -1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // For MacOS
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Open a window and create its OpenGL context
	window = glfwCreateWindow(1024, 768, "Final", NULL, NULL);
	if (window == NULL)
	{
		std::cerr << "Failed to open a GLFW window." << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	// Ensure we can capture the escape key being pressed below
	glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);
	glfwSetKeyCallback(window, key_callback);

	// Load OpenGL functions, gladLoadGL returns the loaded version, 0 on error.
	int version = gladLoadGL(glfwGetProcAddress);
	if (version == 0)
	{
		std::cerr << "Failed to initialize OpenGL context." << std::endl;
		return -1;
	}

	// Background
	glClearColor(0.2f, 0.2f, 0.25f, 0.0f);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	Background background;
	background.init();

	Sun sun;
	sun.initialize("../final/model/sun/sun.gltf"); 
	sun.scale = 10.0f;
	sun.position.z = -100.0f;

	Floor floor; 
	floor.initialize(glm::vec3(0, -25, 0), glm::vec3(300, 1, 300));

	// Initialize random engine
	std::random_device rd;
	std::mt19937 gen(rd());

	// Define world bounds for now (e.g., -100 to 100)
	std::uniform_real_distribution<float> dist(-100.0f, 100.0f);

	std::vector<Model> palms;
	for (int i = 0; i < 30; i++) {
		float xPos, zPos;
		Model palm; 
		palm.initialize("../final/model/palm_tree/palm_tree.gltf");
		palm.position.y = -25.0f;
		palm.scale = 2.0f;
		// Logic for X (Excluding -30 to 30)
		do { xPos = dist(gen); } while (xPos > -30.0f && xPos < 30.0f);
		palm.position.x = xPos;

		// Z can be completely random
		zPos = dist(gen);
		palm.position.z = zPos;

		palms.push_back(palm);
	}
	
	std::vector<Model> pillars;
	for(int i = 0; i < 5; i++) {
		Model pillarLeft;
		pillarLeft.initialize("../final/model/marble_pillar/scene.gltf");
		pillarLeft.rotation.x = glm::radians(90.0f);
		pillarLeft.scale = 0.5f;
		pillarLeft.position.y = -25.0f;
		pillarLeft.position.z = -(float)i * 50.0f; 
		
		pillarLeft.position.x = -30.0f; // Left side
		pillars.push_back(pillarLeft);

		Model pillarRight = pillarLeft; 
		pillarRight.position.x = 30.0f; 
		pillars.push_back(pillarRight);
	}

	Model bust;
	bust.initialize("../final/model/helios_vaporwave_bust/scene.gltf");
	bust.rotation.x = -glm::radians(90.0f);
	bust.scale = 0.8f;
    
	MyBot astronaut;
	astronaut.initialize();
	// Camera setup, set eye location 
    // eye_center.y = viewDistance * cos(viewPolar);
    // eye_center.x = viewDistance * cos(viewAzimuth);
    // eye_center.z = viewDistance * sin(viewAzimuth);

	ScreenQuad bloom;
	bloom.initialize();
	
	//load blur shaders 
	GLuint blurShaderProgram = LoadShadersFromFile("../final/blur.vert", "../final/blur.frag");

	if (blurShaderProgram == 0)
		{
			std::cerr << "Failed to load shaders." << std::endl;
		}
	
	// Get the location of the "horizontal" uniform so we can toggle it
	GLuint horizontalLoc = glGetUniformLocation(blurShaderProgram, "horizontal");
	
	GLuint brightShader = LoadShadersFromFile("../final/bright.vert", "../final/bright.frag");

	if (brightShader == 0)
		{
			std::cerr << "Failed to load shaders." << std::endl;
		}

	bloomFBOinit(1024, 768);
	initBlurFBOs(1024, 768);

	// camera setup 
	glm::mat4 viewMatrix, projectionMatrix;
	projectionMatrix = glm::perspective(glm::radians(FoV), 4.0f / 3.0f, zNear, zFar);
	
	// light setup 
	glm::mat4 lightView, lightProjection;
	// lightProjection = glm::perspective(glm::radians(depthFoV), (float)shadowMapWidth / shadowMapHeight, depthNear, depthFar);
	lightProjection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, zNear, zFar);
	glm::vec3 sunDirection = glm::normalize(glm::vec3(0.0f, -0.05f, 0.09f));
	float distance = 150.0f;

	initializeFBO(shadowMapWidth, shadowMapHeight);

	// Time and frame rate tracking
	static double lastTime = glfwGetTime();
	float time = 0.0f;			// Animation time 
	float fTime = 0.0f;			// Time for measuring fps
	unsigned long frames = 0;

	do
	{
		//render scene to HDR framebuffer
		glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f); //clear hdr buffer 

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Update states for animation
        double currentTime = glfwGetTime();
        float deltaTime = float(currentTime - lastTime);
		lastTime = currentTime;

		//update bust rotation
		bust.update(deltaTime, 2.0f); // Rotate at 10 degrees per second

		//astronaut skeletal animation update
		if (playAnimation) {
			time += deltaTime * playbackSpeed;
			astronaut.update(time);
		}

		//move floor with player
		floor.position.x = eye_center.x;
		floor.position.z = eye_center.z;

		viewMatrix = glm::lookAt(eye_center, lookat, up);
		glm::mat4 vp = projectionMatrix * viewMatrix;

		//light view matrix
		lightPosition = eye_center - (sunDirection * distance);
		lightView = glm::lookAt(lightPosition, eye_center, lightUp);
		glm::mat4 lightVp = lightProjection * lightView;

		// --- PASS 1: Render depth of scene to texture (from light's perspective) ---
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, shadowMapWidth, shadowMapHeight);
		glClear(GL_DEPTH_BUFFER_BIT);
		// glDisable(GL_CULL_FACE); // Disable culling so we get both sides of the triangle for the depth map
		// Use a simplified shadow pass (culling front faces helps prevent acne)
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		
		for(auto& palm : palms) {
			palm.render(true, vp, lightVp);
		}
		for(auto& pillar : pillars) {
			pillar.render(true, vp, lightVp);
		}
		bust.render(true, vp, lightVp);
		astronaut.render(lightVp);

		// --- PASS 2: Render scene as normal using the generated depth/shadow map ---
		glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
		// glEnable(GL_CULL_FACE); // Re-enable culling for normal rendering
		glViewport(0, 0, windowWidth, windowHeight);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glCullFace(GL_BACK);

		// Bind the shadow map texture generated in Pass 1
		glActiveTexture(GL_TEXTURE1); 
		glBindTexture(GL_TEXTURE_2D, depthTex); // The texture attached to 'fbo'

		//render background
		background.draw(glm::vec3(0.557f, 0.388f, 0.831f), glm::vec3(0.969f, 0.329f, 0.714f));

		//render models and floor
		glDepthFunc(GL_LEQUAL); // Change depth function for skybox/ background
		sun.render(viewMatrix, projectionMatrix);
		glDepthFunc(GL_LESS); // Set it back to default

		glDisable(GL_CULL_FACE);
		floor.render(vp, lightVp, eye_center, glm::vec3(10.0f, 0.0f, 10.0f), glm::vec3(0.145f, 0.086f, 0.169f), 30.0f);
		// floor.render(vp, glm::vec3(10.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f), 30.0f);
		glEnable(GL_CULL_FACE);

		for(auto& palm : palms) {
			palm.render(false, vp, lightVp);
		}

		for(auto& pillar : pillars) {
			pillar.render(false, vp, lightVp);
		}

		bust.render(false, vp, lightVp);

		astronaut.render(vp); //modify render function for shadow mapping later

		//filter bright areas 
		glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[0]);
		glUseProgram(brightShader);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, colorBuffer); 
		bloom.draw(); 

		// Bloom post-processing 
		bool horizontal = true, first_iteration = true;
		int amount = 10; // Blur iterations
		glUseProgram(blurShaderProgram);
		
		for (unsigned int i = 0; i < amount; i++) {
			glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[horizontal]);
			glUniform1i(glGetUniformLocation(blurShaderProgram, "horizontal"), horizontal);
			
			// Bind texture from HDR FBO on first pass, then swap between ping-pongs
			glBindTexture(GL_TEXTURE_2D, first_iteration ? colorBuffer : pingpongColorbuffers[!horizontal]);
			
			// Use the ScreenQuad's VAO to draw a full-screen triangle for the blur
			glBindVertexArray(bloom.vertexArrayID);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)0);
			
			horizontal = !horizontal;
			if (first_iteration) first_iteration = false;
		}

		// --- PASS 3: Composite Bloom onto Screen ---
		glBindFramebuffer(GL_FRAMEBUFFER, 0); // Back to the actual screen
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// This calls your ScreenQuad::render which uses bloom_final.frag
		// colorBuffer = sharp scene, pingpongColorbuffers = blurred glow
		bloom.render(colorBuffer, pingpongColorbuffers[!horizontal], 1.0f);

		// Update camera
		// viewAzimuth += 0.1f * deltaTime;
		// if (viewAzimuth > 2 * M_PI) viewAzimuth -= 2 * M_PI;
		// eye_center.x = viewDistance * cos(viewAzimuth);
		// eye_center.z = viewDistance * sin(viewAzimuth);

		// FPS tracking 
		// Count number of frames over a few seconds and take average
		frames++;
		fTime += deltaTime;
		if (fTime > 2.0f) {		
			float fps = frames / fTime;
			frames = 0;
			fTime = 0;
			
			std::stringstream stream;
			stream << std::fixed << std::setprecision(2) << "Frames per second (FPS): " << fps;
			glfwSetWindowTitle(window, stream.str().c_str());
		}

		//save depth map 
		if (saveDepth) {
            std::string filename = "depth_camera.png";
            saveDepthTexture(fbo, filename);
            std::cout << "Depth texture saved to " << filename << std::endl;
            saveDepth = false;
        }

		// Swap buffers
		glfwSwapBuffers(window);
		glfwPollEvents();

	} // Check if the ESC key was pressed or the window was closed
	while (!glfwWindowShouldClose(window));

	background.cleanup();
	sun.cleanup();
	floor.cleanup();
	for(auto& palm : palms) {
		palm.cleanup();
	}
	for(auto& pillar : pillars) {
		pillar.cleanup();
	}
	bust.cleanup();
	astronaut.cleanup();
	bloom.cleanup();

	// Close OpenGL window and terminate GLFW
	glfwTerminate();

	return 0;
}

// Is called whenever a key is pressed/released via GLFW
void key_callback(GLFWwindow *window, int key, int scancode, int action, int mode)
{
	if (key == GLFW_KEY_R && action == GLFW_PRESS)
	{
		viewAzimuth = 0.f;
		viewPolar = 0.f;
		eye_center = glm::vec3(-2.91998, -2.91998, 99.9574);
		std::cout << "Reset." << std::endl;
	}

	// Sensitivity constants
	const float angleStep = 0.05f; 
	const float PI = 3.1415926535f;

	if (key == GLFW_KEY_UP && (action == GLFW_REPEAT || action == GLFW_PRESS))
	{
		viewPolar -= angleStep;
		// Clamp to avoid flipping at the very top (0 degrees)
		if (viewPolar < 0.01f) viewPolar = 0.01f;
	}

	if (key == GLFW_KEY_DOWN && (action == GLFW_REPEAT || action == GLFW_PRESS))
	{
		viewPolar += angleStep;
		// Clamp to avoid flipping at the very bottom (180 degrees)
		if (viewPolar > PI - 0.01f) viewPolar = PI - 0.01f;
	}

	if (key == GLFW_KEY_LEFT && (action == GLFW_REPEAT || action == GLFW_PRESS))
	{
		viewAzimuth -= angleStep;
	}

	if (key == GLFW_KEY_RIGHT && (action == GLFW_REPEAT || action == GLFW_PRESS))
	{
		viewAzimuth += angleStep;
	}

	eye_center.x = lookat.x + viewDistance * sin(viewPolar) * cos(viewAzimuth);
	eye_center.y = lookat.y + viewDistance * cos(viewPolar);
	eye_center.z = lookat.z + viewDistance * sin(viewPolar) * sin(viewAzimuth);

	// For moving forward and backwards 
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
		eye_center += forward * cameraSpeed;
		lookat     += forward * cameraSpeed; // Move the target with the eye
	}

	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
		eye_center -= forward * cameraSpeed;
		lookat     -= forward * cameraSpeed; // Move the target with the eye
	}

	//check camera params
	if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
		std::cout << "--- Camera Debug ---" << std::endl;
		std::cout << "eye_center: " << eye_center.x << ", " << eye_center.y << ", " << eye_center.z << std::endl;
		std::cout << "lookat:     " << lookat.x << ", " << lookat.y << ", " << lookat.z << std::endl;
		std::cout << "up:         " << up.x << ", " << up.y << ", " << up.z << std::endl;
		std::cout << "--------------------" << std::endl;
	}

	if (key == GLFW_KEY_SPACE && (action == GLFW_REPEAT || action == GLFW_PRESS)) 
    {
        saveDepth = true;
    }

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);
}
