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

// 3d Model loading
struct Model {
	// Shader variable IDs
	GLuint mvpMatrixID;
	GLuint programID;

	tinygltf::Model model;

	//for position and rotation in render function 
	glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Euler angles
    float scale = 1.0f;

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
		programID = LoadShadersFromFile("../final/model.vert", "../final/model.frag");
		if (programID == 0)
		{
			std::cerr << "Failed to load shaders." << std::endl;
		}

		// Get a handle for GLSL variables
		mvpMatrixID = glGetUniformLocation(programID, "MVP");
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
			glUniform1i(glGetUniformLocation(programID, "u_BaseColorTexture"), 0);

			glDrawElements(primitive.mode, indexAccessor.count,
						indexAccessor.componentType,
						BUFFER_OFFSET(indexAccessor.byteOffset));

			glBindVertexArray(0);
		}
	}

	void drawModelNodes(const std::vector<PrimitiveObject>& primitiveObjects,
						tinygltf::Model &model, tinygltf::Node &node) {
		// Draw the mesh at the node, and recursively do so for children nodes
		if ((node.mesh >= 0) && (node.mesh < model.meshes.size())) {
			drawMesh(primitiveObjects, model, model.meshes[node.mesh]);
		}
		for (size_t i = 0; i < node.children.size(); i++) {
			drawModelNodes(primitiveObjects, model, model.nodes[node.children[i]]);
		}
	}
	void drawModel(const std::vector<PrimitiveObject>& primitiveObjects,
				tinygltf::Model &model) {
		// Draw all nodes
		const tinygltf::Scene &scene = model.scenes[model.defaultScene];
		for (size_t i = 0; i < scene.nodes.size(); ++i) {
			drawModelNodes(primitiveObjects, model, model.nodes[scene.nodes[i]]);
		}
	}

	void render(glm::mat4 cameraMatrix) {
		glUseProgram(programID);
		
		//model transforms 
		glm::mat4 modelMat = glm::mat4(1.0f);
        modelMat = glm::translate(modelMat, position);
        modelMat = glm::rotate(modelMat, rotation.x, glm::vec3(1,0,0));
        modelMat = glm::rotate(modelMat, rotation.y, glm::vec3(0,1,0));
        modelMat = glm::rotate(modelMat, rotation.z, glm::vec3(0,0,1));
        modelMat = glm::scale(modelMat, glm::vec3(scale));

		// Set camera
		glm::mat4 mvp = cameraMatrix * modelMat;
		glUniformMatrix4fv(mvpMatrixID, 1, GL_FALSE, &mvp[0][0]);

		// Draw the GLTF model
		drawModel(primitiveObjects, model);
	}

	void cleanup() {
		glDeleteProgram(programID);
	}
}; 

static GLuint LoadTextureSkybox(const char *texture_file_path) {
    int w, h, channels;
    uint8_t* img = stbi_load(texture_file_path, &w, &h, &channels, 3);
    GLuint texture;
	// Generate an OpenGL texture ID and make use of it 
    glGenTextures(1, &texture);  
    glBindTexture(GL_TEXTURE_2D, texture);  

    // To tile textures on a box, we set wrapping to repeat
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);	
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); //apparently changing to GL nearest removes the seams
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (img) {
		// Load the image into the current OpenGL texture 
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, img);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        std::cout << "Failed to load texture " << texture_file_path << std::endl;
    }
    stbi_image_free(img);

    return texture;
}

// skybox 
struct Skybox {
	glm::vec3 position;		// Position of the box 
	glm::vec3 scale;		// Size of the box in each axis
	
	GLfloat vertex_buffer_data[72] = {	// Vertex definition for a canonical box
		// Front face x+
		-1.0f, -1.0f, 1.0f, 
		1.0f, -1.0f, 1.0f, 
		1.0f, 1.0f, 1.0f, 
		-1.0f, 1.0f, 1.0f, 
		
		// Back face x-
		1.0f, -1.0f, -1.0f, 
		-1.0f, -1.0f, -1.0f, 
		-1.0f, 1.0f, -1.0f, 
		1.0f, 1.0f, -1.0f,
		
		// Left face z-
		-1.0f, -1.0f, -1.0f, 
		-1.0f, -1.0f, 1.0f, 
		-1.0f, 1.0f, 1.0f, 
		-1.0f, 1.0f, -1.0f, 

		// Right face z+
		1.0f, -1.0f, 1.0f, 
		1.0f, -1.0f, -1.0f, 
		1.0f, 1.0f, -1.0f, 
		1.0f, 1.0f, 1.0f,

		// Top face y+
		-1.0f, 1.0f, 1.0f, 
		1.0f, 1.0f, 1.0f, 
		1.0f, 1.0f, -1.0f, 
		-1.0f, 1.0f, -1.0f, 

		// Bottom face y-
		-1.0f, -1.0f, -1.0f, 
		1.0f, -1.0f, -1.0f, 
		1.0f, -1.0f, 1.0f, 
		-1.0f, -1.0f, 1.0f, 
	};

	GLfloat color_buffer_data[72] = {
		// Front, red
		1.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,

		// Back, yellow
		1.0f, 1.0f, 0.0f,
		1.0f, 1.0f, 0.0f,
		1.0f, 1.0f, 0.0f,
		1.0f, 1.0f, 0.0f,

		// Left, green
		0.0f, 1.0f, 0.0f, 
		0.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 0.0f,

		// Right, cyan
		0.0f, 1.0f, 1.0f, 
		0.0f, 1.0f, 1.0f, 
		0.0f, 1.0f, 1.0f, 
		0.0f, 1.0f, 1.0f, 

		// Top, blue
		0.0f, 0.0f, 1.0f, 
		0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 1.0f,

		// Bottom, magenta
		1.0f, 0.0f, 1.0f,
		1.0f, 0.0f, 1.0f, 
		1.0f, 0.0f, 1.0f, 
		1.0f, 0.0f, 1.0f,  
	};

    GLuint index_buffer_data[36] = {		// 12 triangle faces of a box
		2, 1, 0, 	
		3, 2, 0, 
		
		6, 5, 4, 
		7, 6, 4, 

		10, 9, 8, 
		11, 10, 8, 

		14, 13, 12, 
		15, 14, 12, 

		18, 17, 16, 
		19, 18, 16, 

		22, 21, 20, 
		23, 22, 20, 
	};

    // TODO: Define UV buffer data
    GLfloat uv_buffer_data[48] = {
		// Front face x+
        0.25f, 2.0f/3.0f,
		0.0f, 2.0f/3.0f,
        0.0f, 1.0f/3.0f,
        0.25f, 1.0f/3.0f,
		
		// Back face x-
        0.75f, 2.0f/3.0f,
		0.5f, 2.0f/3.0f,
        0.5f, 1.0f/3.0f,
        0.75f, 1.0f/3.0f,
		
		// Left face z-
        0.5f, 2.0f/3.0f,
		0.25f, 2.0f/3.0f,
        0.25f, 1.0f/3.0f,
        0.5f, 1.0f/3.0f,
		
		// Right face z+
        1.0f, 2.0f/3.0f,
		0.75f, 2.0f/3.0f,
        0.75f, 1.0f/3.0f,
        1.0f, 1.0f/3.0f,
		
		// Top face y-
        0.25f, 1.0f/3.0f,
        0.25f, 0.0f,
        0.5f, 0.0f,
        0.5f, 1.0f/3.0f,
       
		// Bottom face y+
        0.5f, 2.0f/3.0f,
        0.5f, 1.0f,
        0.25f, 1.0f,
        0.25f, 2.0f/3.0f,
    
	}; 

	// OpenGL buffers
	GLuint vertexArrayID; 
	GLuint vertexBufferID; 
	GLuint indexBufferID; 
	GLuint colorBufferID;
	GLuint uvBufferID;
	GLuint textureID;

	// Shader variable IDs
	GLuint mvpMatrixID;
	GLuint textureSamplerID;
	GLuint programID;

	void initialize(glm::vec3 position, glm::vec3 scale) {
		// Define scale of the building geometry
		this->position = position;
		this->scale = scale;

		// Create a vertex array object
		glGenVertexArrays(1, &vertexArrayID);
		glBindVertexArray(vertexArrayID);

		// Create a vertex buffer object to store the vertex data		
		glGenBuffers(1, &vertexBufferID);
		glBindBuffer(GL_ARRAY_BUFFER, vertexBufferID);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_buffer_data), vertex_buffer_data, GL_STATIC_DRAW);

		// Create a vertex buffer object to store the color data
        // TODO: 
		glGenBuffers(1, &colorBufferID);
		glBindBuffer(GL_ARRAY_BUFFER, colorBufferID);
		glBufferData(GL_ARRAY_BUFFER, sizeof(color_buffer_data), color_buffer_data, GL_STATIC_DRAW);

		// for (int i=0; i<24; ++i) uv_buffer_data[2*i+1] *= scale.y / scale.x; //understand this later lol but i think this repeats the pattern 5 times?

        // int texWidth, texHeight, texChannels;
        // stbi_info("../lab2/studio_garden.png", &texWidth, &texHeight, &texChannels);
        // float offsetX = 0.5f / texWidth;
        // float offsetY = 0.5f / texHeight;

        // updateUVs(uv_buffer_data, offsetX, offsetY);

		// TODO: Create a vertex buffer object to store the UV data
		glGenBuffers(1, &uvBufferID);
		glBindBuffer(GL_ARRAY_BUFFER, uvBufferID);
		glBufferData(GL_ARRAY_BUFFER, sizeof(uv_buffer_data), uv_buffer_data, GL_STATIC_DRAW);

		// Create an index buffer object to store the index data that defines triangle faces
		glGenBuffers(1, &indexBufferID);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferID);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_buffer_data), index_buffer_data, GL_STATIC_DRAW);

		// Create and compile our GLSL program from the shaders
		programID = LoadShadersFromFile("../final/box.vert", "../final/box.frag");
		if (programID == 0)
		{
			std::cerr << "Failed to load shaders." << std::endl;
		}

		// Get a handle for our "MVP" uniform
		mvpMatrixID = glGetUniformLocation(programID, "MVP");

        // TODO: Load a texture 
        // textureID = LoadTextureSkybox("../lab2/studio_garden_debug.png");
        // textureID = LoadTextureSkybox("../lab2/studio_garden.png");
        textureID = LoadTextureSkybox("../final/space2.png");

        // TODO: Get a handle to texture sampler 
        textureSamplerID = glGetUniformLocation(programID, "textureSampler");

	}

	void render(glm::mat4 cameraMatrix) {
		glUseProgram(programID);

		glDepthMask(GL_FALSE);

		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, vertexBufferID);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, colorBufferID);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferID);

		// TODO: Model transform 
		// -----------------------
        glm::mat4 modelMatrix = glm::mat4();
		modelMatrix = glm::translate(modelMatrix, position);   
		modelMatrix = glm::scale(modelMatrix, scale);
        
        // -----------------------

		// Set model-view-projection matrix
		glm::mat4 mvp = cameraMatrix * modelMatrix;
		glUniformMatrix4fv(mvpMatrixID, 1, GL_FALSE, &mvp[0][0]);

		// TODO: Enable UV buffer and texture sampler
		glEnableVertexAttribArray(2);
		glBindBuffer(GL_ARRAY_BUFFER, uvBufferID);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, 0);

		// set textureSampler to use Texture Unit 0
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textureID);
		glUniform1i(textureSamplerID, 0);

		// Draw the box
		glDrawElements(
			GL_TRIANGLES,      // mode
			36,    			   // number of indices
			GL_UNSIGNED_INT,   // type
			(void*)0           // element array buffer offset
		);

		glDepthMask(GL_TRUE);

		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
        //glDisableVertexAttribArray(2);
	}

	void cleanup() {
		glDeleteBuffers(1, &vertexBufferID);
		glDeleteBuffers(1, &colorBufferID);
		glDeleteBuffers(1, &indexBufferID);
		glDeleteVertexArrays(1, &vertexArrayID);
		//glDeleteBuffers(1, &uvBufferID);
		//glDeleteTextures(1, &textureID);
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

	// create skybox
    Skybox skybox;
    skybox.initialize(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(600.0f, 600.0f, 600.0f));

	Model sun;
	sun.initialize("../final/model/sun/sun.gltf"); 
	sun.scale = 10.0f;
	sun.position.z = -100.0f;

	// Initialize random engine
	std::random_device rd;
	std::mt19937 gen(rd());

	// Define your total world bounds (e.g., -100 to 100)
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
	bust.scale = 0.5f;
    // ---------------------------

	// Camera setup, set eye location 
    // eye_center.y = viewDistance * cos(viewPolar);
    // eye_center.x = viewDistance * cos(viewAzimuth);
    // eye_center.z = viewDistance * sin(viewAzimuth);

	glm::mat4 viewMatrix, projectionMatrix;
    
	projectionMatrix = glm::perspective(glm::radians(FoV), 4.0f / 3.0f, zNear, zFar);

	// Time and frame rate tracking
	static double lastTime = glfwGetTime();
	float time = 0.0f;			// Animation time 
	float fTime = 0.0f;			// Time for measuring fps
	unsigned long frames = 0;

	do
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Update states for animation
        double currentTime = glfwGetTime();
        float deltaTime = float(currentTime - lastTime);
		lastTime = currentTime;

		viewMatrix = glm::lookAt(eye_center, lookat, up);
		glm::mat4 vp = projectionMatrix * viewMatrix;

		// Render the skybox
		skybox.render(vp);

		//render models
		sun.render(vp);
		for(auto& palm : palms) {
			palm.render(vp);
		}

		for(auto& pillar : pillars) {
			pillar.render(vp);
		}
		bust.render(vp);

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

		// Swap buffers
		glfwSwapBuffers(window);
		glfwPollEvents();

	} // Check if the ESC key was pressed or the window was closed
	while (!glfwWindowShouldClose(window));

	// Clean up
	skybox.cleanup();
	sun.cleanup();
	for(auto& palm : palms) {
		palm.cleanup();
	}
	for(auto& pillar : pillars) {
		pillar.cleanup();
	}
	bust.cleanup();

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

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);
}
