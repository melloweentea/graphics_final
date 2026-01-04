#version 330 core

// Input
// layout(location = 0) in vec3 vertexPosition;
// layout(location = 1) in vec3 vertexNormal;
// layout(location = 2) in vec2 vertexUV;
// layout(location = 3) in vec4 boneIDs;      // Joint indices (location 3)
// layout(location = 4) in vec4 boneWeights;   // Joint weights (location 4)
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec2 vertexUV; 
layout(location = 3) in ivec4 boneIDs;   
layout(location = 4) in vec4 boneWeights;

// --- New Uniforms for Skinning ---
uniform mat4 MVP;
uniform mat4 u_model;                    // Model matrix (needed for worldPosition/Normal)
uniform mat4 jointMatrices[100];       // Array of Final Joint Matrices (size must be >= max joints)

// Output data, to be interpolated for each fragment
out vec3 worldPosition;
out vec3 worldNormal;

void main() {
    // 1. Calculate the final Bone Transformation Matrix (Blend)
    // mat4 boneTransform = 
    //     boneWeights.x * jointMatrices[int (boneIDs.x)] +
    //     boneWeights.y * jointMatrices[int (boneIDs.y)] +
    //     boneWeights.z * jointMatrices[int (boneIDs.z)] +
    //     boneWeights.w * jointMatrices[int (boneIDs.w)];
    mat4 boneTransform = 
        boneWeights.x * jointMatrices[boneIDs.x] +
        boneWeights.y * jointMatrices[boneIDs.y] +
        boneWeights.z * jointMatrices[boneIDs.z] +
        boneWeights.w * jointMatrices[boneIDs.w];

    // mat4 boneTransform = mat4(1.0);

    // 2. Transform the Skinned Vertex Position
    vec4 skinnedPos = boneTransform * vec4(vertexPosition, 1.0);
    
    // 3. Apply MVP Transformation (to Clip Space)
    gl_Position = MVP * skinnedPos;

    // 4. Transform Normals (using the Model Matrix and Bone Transform)
    // The Normal Matrix requires the inverse transpose of the combined Model and Bone transform.
    mat3 normalMatrix = transpose(inverse(mat3(u_model * boneTransform)));
    worldNormal = normalMatrix * vertexNormal;
    
    // 5. Calculate World Position
    worldPosition = vec3(u_model * skinnedPos);
}
