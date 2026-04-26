#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "yara.h"

#include "util.h"
#include "HandmadeMath.h"
#include "ufbx.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#pragma warning(push, 0)
#include "mikktspace.c"
#pragma warning(pop)

static int DoneRunning;

enum Key_State
{
    RELEASED,
    PRESSED,
    HELD,
};
static enum Key_State keyboard_input[255] = {0};
LRESULT CALLBACK WindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;
    switch (Message)
    {
        case WM_CLOSE:
        case WM_QUIT:
        {
            DoneRunning = 1;
        } break;
        case WM_KEYDOWN:
        {
            switch (keyboard_input[WParam])
            {
            case PRESSED:
                keyboard_input[WParam] = HELD;
                break;
            case RELEASED:
                keyboard_input[WParam] = PRESSED;
                break;
            }
        } break;
        case WM_KEYUP:
        {
            keyboard_input[WParam] = RELEASED;
        } break;

        default:
        {
            Result = DefWindowProc(Window, Message, WParam, LParam);
        } break;
    }
    return Result;
}

struct Texture
{
    char* path;
    struct Buffer* buffer;
    struct Shader_Resource_View* srv;
};
enum NODE_TYPE 
{
    NODE_TYPE_EMPTY,
    NODE_TYPE_MESH,
    NODE_TYPE_LIGHT_POINT,
    NODE_TYPE_LIGHT_SPOT,
    NODE_TYPE_LIGHT_DIRECTIONAL,
    NODE_TYPE_CAMERA,
};
struct Node
{
    enum NODE_TYPE type;
    union 
    {
        struct 
        {
            struct Mesh_Part* mesh_parts;
            size_t mesh_parts_count;
        } mesh;
        struct 
        {
            Vec3 color;
            float range;
        } light_point;
        struct 
        {
            Vec3 color;
            float range;
            float angle;
        } light_spot;
        struct 
        {
            Vec3 color;
        } light_directional;
    };

    char* name;
    Vec3 local_position;
    Quat local_rotation;
    Vec3 local_scale;
    Vec3 geometry_position;
    Quat geometry_rotation;
    Vec3 geometry_scale;
    struct Node* parent;
    struct Node** child_array;
    size_t child_count;

    struct Texture* texture_array;
    size_t texture_count;
};
struct Node* node_create()
{
    struct Node* node = calloc(sizeof(struct Node), 1);
    return node;
}
Mat4 node_local_transform(struct Node* node)
{
    Mat4 translation = Translate(node->local_position);
    Mat4 rotation = QToM4(node->local_rotation);
    Mat4 scale = Scale(node->local_scale);
    return MulM4(translation, MulM4(rotation, scale));
}
Mat4 node_geometry_transform(struct Node* node)
{
    Mat4 translation = Translate(node->geometry_position);
    Mat4 rotation = QToM4(node->geometry_rotation);
    Mat4 scale = Scale(node->geometry_scale);
    return MulM4(translation, MulM4(rotation, scale));
}
Mat4 node_global_transform(struct Node* node)
{
    Mat4 parent = M4D(1.0f);
    if (node->parent)
    {
        parent = node_global_transform(node->parent);
    }
    
    return MulM4(parent, node_local_transform(node));
}
Mat4 node_global_transform_geometry(struct Node* node)
{
    return MulM4(node_global_transform(node), node_geometry_transform(node));
}

#define conv_float(in, out) for (size_t conv_i = 0; conv_i < ARRAYSIZE(in.v); conv_i++) { out.Elements[conv_i] = (float)in.v[conv_i]; }
#define conv_double(in, out) for (size_t conv_i = 0; conv_i < ARRAYSIZE(in.v); conv_i++) { out.Elements[conv_i] = (double)in.v[conv_i]; }

#pragma pack(push, 1)
struct Vertex 
{
    Vec3 pos;
    Vec4 color;
    Vec3 normal;
    Vec4 tangent;
    Vec2 uv;
};
#pragma pack(pop)
struct Mesh_Part
{
    struct Vertex* vertex_array;
    size_t vertex_count;

    unsigned int* index_array;
    size_t index_count;

    struct Buffer* vertex_buffer;
    struct Buffer* index_buffer;

    struct Buffer* constant_buffer;
    struct Constant_Buffer_View* cbv;

    struct Texture* color_texture;
    struct Texture* normal_texture;
};
int mikkt_get_num_faces(const SMikkTSpaceContext *ctx) {
    struct Mesh_Part *mesh = (struct Mesh_Part*)ctx->m_pUserData;
    return (int)mesh->index_count / 3;
}
int mikkt_get_num_vertices_of_face(const SMikkTSpaceContext *ctx, int face) {
    (void)ctx; (void)face;
    return 3; // all triangles
}
void mikkt_get_position(const SMikkTSpaceContext *ctx, float out[], int face, int vert) {
    struct Mesh_Part *mesh = (struct Mesh_Part*)ctx->m_pUserData;
    int idx = mesh->index_array[face * 3 + vert];
    memcpy(out, mesh->vertex_array[idx].pos.Elements, sizeof(float) * 3);
}
void mikkt_get_normal(const SMikkTSpaceContext *ctx, float out[], int face, int vert) {
    struct Mesh_Part *mesh = (struct Mesh_Part*)ctx->m_pUserData;
    int idx = mesh->index_array[face * 3 + vert];
    memcpy(out, mesh->vertex_array[idx].normal.Elements, sizeof(float) * 3);
}
void mikkt_get_tex_coord(const SMikkTSpaceContext *ctx, float out[], int face, int vert) {
    struct Mesh_Part *mesh = (struct Mesh_Part*)ctx->m_pUserData;
    int idx = mesh->index_array[face * 3 + vert];
    // out[0] = mesh->vertex_array[idx].uv.Elements[0];
    // out[1] = mesh->vertex_array[idx].uv.Elements[1];
    // out[1] = 1.0f - mesh->vertex_array[idx].uv.Elements[1];
    memcpy(out, mesh->vertex_array[idx].uv.Elements, sizeof(float) * 2);
}
void mikkt_set_t_space_basic(const SMikkTSpaceContext *ctx, const float tangent[], float sign, int face, int vert) {
    struct Mesh_Part *mesh = (struct Mesh_Part*)ctx->m_pUserData;
    int idx = mesh->index_array[face * 3 + vert];
    mesh->vertex_array[idx].tangent.Elements[0] = tangent[0];
    mesh->vertex_array[idx].tangent.Elements[1] = tangent[1];
    mesh->vertex_array[idx].tangent.Elements[2] = tangent[2];
    mesh->vertex_array[idx].tangent.Elements[3] = sign; // bitangent = cross(normal, tangent) * sign
}
struct Mesh_Part load_mesh_part(ufbx_mesh *mesh, ufbx_mesh_part *part, size_t material_index, struct Node* node)
{
    size_t num_triangles = part->num_triangles;
    struct Vertex *vertices = calloc(num_triangles * 3, sizeof(struct Vertex));
    size_t num_vertices = 0;

    size_t num_tri_indices = mesh->max_face_triangles * 3;
    uint32_t *tri_indices = calloc(num_tri_indices, sizeof(uint32_t));

    for (size_t face_ix = 0; face_ix < part->num_faces; face_ix++) 
    {
        ufbx_face face = mesh->faces.data[part->face_indices.data[face_ix]];

        uint32_t num_tris = ufbx_triangulate_face(tri_indices, num_tri_indices, mesh, face);

        for (size_t i = 0; i < num_tris * 3; i++) 
        {
            uint32_t index = tri_indices[i];

            struct Vertex *v = &vertices[num_vertices++];
            if (mesh->vertex_position.exists)
            {
                ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                conv_float(pos, v->pos);
            }
            if (mesh->vertex_color.exists)
            {
                ufbx_vec4 color = ufbx_get_vertex_vec4(&mesh->vertex_color, index);
                conv_float(color, v->color);
            }
            else
            {
                v->color = V4(1.0f, 1.0f, 1.0f, 1.0f);
            }
            if (mesh->vertex_normal.exists)
            {
                ufbx_vec3 normal = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                conv_float(normal, v->normal);
            }
            if (mesh->vertex_tangent.exists)
            {
                ufbx_vec3 tangent = ufbx_get_vertex_vec3(&mesh->vertex_tangent, index);
                conv_float(tangent, v->tangent);
                v->tangent.W = (float)ufbx_get_vertex_w_vec3(&mesh->vertex_tangent, index);
            }
            if (mesh->vertex_uv.exists)
            {
                ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                conv_float(uv, v->uv);
            }
        }
    }

    free(tri_indices);
    assert(num_vertices == num_triangles * 3);

    ufbx_vertex_stream streams[1] = {
        { vertices, num_vertices, sizeof(struct Vertex) },
    };
    size_t num_indices = num_triangles * 3;
    uint32_t *indices = calloc(num_indices, sizeof(uint32_t));

    num_vertices = ufbx_generate_indices(streams, 1, indices, num_indices, NULL, NULL);

    struct Mesh_Part mesh_part = {0};
    mesh_part.index_array = indices;
    mesh_part.index_count = num_indices;
    mesh_part.vertex_array = vertices;
    mesh_part.vertex_count = num_vertices;

    // Generate tangents
    if (!mesh->vertex_tangent.exists && mesh_part.index_count > 0 && mesh_part.vertex_count > 0)
    {
        SMikkTSpaceInterface mikkt_interface = {0};
        mikkt_interface.m_getNumFaces          = mikkt_get_num_faces;
        mikkt_interface.m_getNumVerticesOfFace = mikkt_get_num_vertices_of_face;
        mikkt_interface.m_getPosition          = mikkt_get_position;
        mikkt_interface.m_getNormal            = mikkt_get_normal;
        mikkt_interface.m_getTexCoord          = mikkt_get_tex_coord;
        mikkt_interface.m_setTSpaceBasic       = mikkt_set_t_space_basic;
        // mikkt_interface.m_setTSpace         = NULL;  // use setTSpaceBasic instead
        
        SMikkTSpaceContext ctx = {0};
        ctx.m_pInterface = &mikkt_interface;
        ctx.m_pUserData  = &mesh_part;
        if (!genTangSpaceDefault(&ctx)) {
            fprintf(stderr, "MikkTSpace failed!\n");
            __debugbreak();
        }
    }

    mesh->materials.data[0]->pbr.base_color.texture;
    mesh->materials.data[0]->pbr.normal_map.texture;
    for (size_t i = 0; i < node->texture_count; i++)
    {
        if (mesh->materials.data[material_index]->pbr.base_color.texture && strcmp(mesh->materials.data[material_index]->pbr.base_color.texture->filename.data, node->texture_array[i].path) == 0)
        {
            mesh_part.color_texture = &node->texture_array[i];
        }
        if (mesh->materials.data[material_index]->pbr.normal_map.texture && strcmp(mesh->materials.data[material_index]->pbr.normal_map.texture->filename.data, node->texture_array[i].path) == 0)
        {
            mesh_part.normal_texture = &node->texture_array[i];
        }
    }

    return mesh_part;
}
struct Node* load_node(ufbx_node* fbx_node, struct Node* root, ufbx_scene* fbx_scene)
{
    printf("Object: %s\n", fbx_node->name.data);

    struct Node* node = node_create();
    node->name = calloc(sizeof(char), fbx_node->name.length+1);
    strcpy(node->name, fbx_node->name.data);
    
    conv_float(fbx_node->local_transform.translation, node->local_position);
    conv_float(fbx_node->local_transform.rotation, node->local_rotation);
    conv_float(fbx_node->local_transform.scale, node->local_scale);

    conv_float(fbx_node->geometry_transform.translation, node->geometry_position);
    conv_float(fbx_node->geometry_transform.rotation, node->geometry_rotation);
    conv_float(fbx_node->geometry_transform.scale, node->geometry_scale);

    if (fbx_node->mesh)
    {
        ufbx_mesh* mesh = fbx_node->mesh;
        printf("-> mesh with %zu faces\n", mesh->faces.count);
        node->type = NODE_TYPE_MESH;
        node->mesh.mesh_parts_count = mesh->material_parts.count;
        node->mesh.mesh_parts = calloc(mesh->material_parts.count, sizeof(struct Mesh_Part));

        for (size_t i = 0; i < mesh->material_parts.count; i++)
        {
            node->mesh.mesh_parts[i] = load_mesh_part(mesh, &mesh->material_parts.data[i], i, root);
        }
    }
    else if (fbx_node->light && fbx_node->light->type == UFBX_LIGHT_POINT) {}
    else if (fbx_node->light && fbx_node->light->type == UFBX_LIGHT_SPOT) {}
    else if (fbx_node->light && fbx_node->light->type == UFBX_LIGHT_DIRECTIONAL) {}
    else if (fbx_node->camera) 
    {
        node->type = NODE_TYPE_CAMERA;
    }
    else 
    {
        node->type = NODE_TYPE_EMPTY;

        if (fbx_node->is_root)
        {
            root = node;

            node->texture_count = fbx_scene->textures.count;
            node->texture_array = calloc(node->texture_count, sizeof(struct Texture));

            for (size_t i = 0; i < node->texture_count; i++)
            {
                ufbx_texture* fbx_texture = fbx_scene->textures.data[i];
                struct Texture* texture = &node->texture_array[i];

                texture->path = get_asset_path(fbx_texture->relative_filename.data);
            }
            
        }
    }

    node->child_array = calloc(fbx_node->children.count, sizeof(struct Node*));
    node->child_count = fbx_node->children.count;
    for (size_t i = 0; i < fbx_node->children.count; i++) 
    {
        node->child_array[i] = load_node(fbx_node->children.data[i], root, fbx_scene);
        node->child_array[i]->parent = node;
        node->child_array[i]->texture_array = node->texture_array;
        node->child_array[i]->texture_count = node->texture_count;
    }

    return node;
}
struct Node* load_fbx(char* path)
{
    ufbx_load_opts opts = {
        .target_axes = {
			.right = UFBX_COORDINATE_AXIS_POSITIVE_X,
			.up = UFBX_COORDINATE_AXIS_POSITIVE_Y,
			.front = UFBX_COORDINATE_AXIS_NEGATIVE_Z, // Could be UFBX_COORDINATE_AXIS_POSITIVE_Z
		},
		.target_unit_meters = 1.0f,
        .generate_missing_normals = TRUE,
        .handedness_conversion_axis = UFBX_MIRROR_AXIS_X, // Might need to be omited.
        .handedness_conversion_retain_winding = TRUE,
        .reverse_winding = TRUE,
        .space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY,
        .retain_vertex_attrib_w = TRUE
    };
    ufbx_error error;
    ufbx_scene *fbx_scene = ufbx_load_file(path, &opts, &error);
    if (!fbx_scene)
    {
        fprintf(stderr, "Failed to load: %s\n", error.description.data);
        exit(1);
    }

    printf("Scene: %s\n", path);

    struct Node* scene = load_node(fbx_scene->root_node, 0, fbx_scene);

    ufbx_free_scene(fbx_scene);
    return scene;
}

void load_texture_png(struct Texture *texture, struct Device *device, struct Descriptor_Set *cbv_srv_uav_descriptor_set, struct Command_List *upload_command_list)
{
    int expected_component_count;
    stbi_info(texture->path, &(int){0}, &(int){0}, &expected_component_count);

    stbi_set_flip_vertically_on_load(1);
    int x;
    int y;
    int component_count;
    unsigned char *image_data = stbi_load(texture->path, &x, &y, &component_count, (expected_component_count == 3) ? 4 : 0);
    if (expected_component_count == 3)
        component_count = 4;

    enum FORMAT formats[] = {FORMAT_UNKNOWN, FORMAT_R8_UNORM, FORMAT_R8G8_UNORM, FORMAT_R8G8B8A8_UNORM, FORMAT_R8G8B8A8_UNORM};

    struct Buffer_Descriptor buffer_description = {
        .width = (unsigned long long)x,
        .height = (unsigned long long)y,
        .buffer_type = BUFFER_TYPE_TEXTRUE2D,
        .bind_types = {
            BIND_TYPE_SRV
        },
        .bind_types_count = 1,
        .format = formats[component_count]};
    device_create_buffer(device, buffer_description, &texture->buffer);

    device_create_shader_resource_view(device, 0, cbv_srv_uav_descriptor_set, texture->buffer, &texture->srv);

    buffer_set_name(texture->buffer, texture->path);

    struct Upload_Buffer *texture_upload_buffer = 0;
    device_create_upload_buffer(device, 0, (unsigned long long)(x * y * sizeof(unsigned char) * component_count), &texture_upload_buffer);

    void *mapped = upload_buffer_map(texture_upload_buffer);
    mapped;

    memcpy(mapped, image_data, sizeof(unsigned char) * component_count * x * y);

    upload_buffer_unmap(texture_upload_buffer);

    stbi_image_free(image_data);

    command_list_copy_upload_buffer_to_buffer(upload_command_list, texture_upload_buffer, texture->buffer);
    upload_buffer_destroy(texture_upload_buffer);
}

void load_texture_dds(struct Texture *texture, struct Device *device, struct Descriptor_Set *cbv_srv_uav_descriptor_set, struct Command_List *upload_command_list)
{
    FILE* file = fopen(texture->path, "rb");
    if (!file)
        __debugbreak();

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);

    char* buffer = malloc(file_size);
    fread(buffer, 1, file_size, file);
    fclose(file);

    printf("buffersize: %zd\n", file_size);

    enum DXGI_FORMAT
    {
        DXGI_FORMAT_UNKNOWN = 0,
        DXGI_FORMAT_R32G32B32A32_TYPELESS = 1,
        DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
        DXGI_FORMAT_R32G32B32A32_UINT = 3,
        DXGI_FORMAT_R32G32B32A32_SINT = 4,
        DXGI_FORMAT_R32G32B32_TYPELESS = 5,
        DXGI_FORMAT_R32G32B32_FLOAT = 6,
        DXGI_FORMAT_R32G32B32_UINT = 7,
        DXGI_FORMAT_R32G32B32_SINT = 8,
        DXGI_FORMAT_R16G16B16A16_TYPELESS = 9,
        DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
        DXGI_FORMAT_R16G16B16A16_UNORM = 11,
        DXGI_FORMAT_R16G16B16A16_UINT = 12,
        DXGI_FORMAT_R16G16B16A16_SNORM = 13,
        DXGI_FORMAT_R16G16B16A16_SINT = 14,
        DXGI_FORMAT_R32G32_TYPELESS = 15,
        DXGI_FORMAT_R32G32_FLOAT = 16,
        DXGI_FORMAT_R32G32_UINT = 17,
        DXGI_FORMAT_R32G32_SINT = 18,
        DXGI_FORMAT_R32G8X24_TYPELESS = 19,
        DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
        DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
        DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,
        DXGI_FORMAT_R10G10B10A2_TYPELESS = 23,
        DXGI_FORMAT_R10G10B10A2_UNORM = 24,
        DXGI_FORMAT_R10G10B10A2_UINT = 25,
        DXGI_FORMAT_R11G11B10_FLOAT = 26,
        DXGI_FORMAT_R8G8B8A8_TYPELESS = 27,
        DXGI_FORMAT_R8G8B8A8_UNORM = 28,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
        DXGI_FORMAT_R8G8B8A8_UINT = 30,
        DXGI_FORMAT_R8G8B8A8_SNORM = 31,
        DXGI_FORMAT_R8G8B8A8_SINT = 32,
        DXGI_FORMAT_R16G16_TYPELESS = 33,
        DXGI_FORMAT_R16G16_FLOAT = 34,
        DXGI_FORMAT_R16G16_UNORM = 35,
        DXGI_FORMAT_R16G16_UINT = 36,
        DXGI_FORMAT_R16G16_SNORM = 37,
        DXGI_FORMAT_R16G16_SINT = 38,
        DXGI_FORMAT_R32_TYPELESS = 39,
        DXGI_FORMAT_D32_FLOAT = 40,
        DXGI_FORMAT_R32_FLOAT = 41,
        DXGI_FORMAT_R32_UINT = 42,
        DXGI_FORMAT_R32_SINT = 43,
        DXGI_FORMAT_R24G8_TYPELESS = 44,
        DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46,
        DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,
        DXGI_FORMAT_R8G8_TYPELESS = 48,
        DXGI_FORMAT_R8G8_UNORM = 49,
        DXGI_FORMAT_R8G8_UINT = 50,
        DXGI_FORMAT_R8G8_SNORM = 51,
        DXGI_FORMAT_R8G8_SINT = 52,
        DXGI_FORMAT_R16_TYPELESS = 53,
        DXGI_FORMAT_R16_FLOAT = 54,
        DXGI_FORMAT_D16_UNORM = 55,
        DXGI_FORMAT_R16_UNORM = 56,
        DXGI_FORMAT_R16_UINT = 57,
        DXGI_FORMAT_R16_SNORM = 58,
        DXGI_FORMAT_R16_SINT = 59,
        DXGI_FORMAT_R8_TYPELESS = 60,
        DXGI_FORMAT_R8_UNORM = 61,
        DXGI_FORMAT_R8_UINT = 62,
        DXGI_FORMAT_R8_SNORM = 63,
        DXGI_FORMAT_R8_SINT = 64,
        DXGI_FORMAT_A8_UNORM = 65,
        DXGI_FORMAT_R1_UNORM = 66,
        DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67,
        DXGI_FORMAT_R8G8_B8G8_UNORM = 68,
        DXGI_FORMAT_G8R8_G8B8_UNORM = 69,
        DXGI_FORMAT_BC1_TYPELESS = 70,
        DXGI_FORMAT_BC1_UNORM = 71,
        DXGI_FORMAT_BC1_UNORM_SRGB = 72,
        DXGI_FORMAT_BC2_TYPELESS = 73,
        DXGI_FORMAT_BC2_UNORM = 74,
        DXGI_FORMAT_BC2_UNORM_SRGB = 75,
        DXGI_FORMAT_BC3_TYPELESS = 76,
        DXGI_FORMAT_BC3_UNORM = 77,
        DXGI_FORMAT_BC3_UNORM_SRGB = 78,
        DXGI_FORMAT_BC4_TYPELESS = 79,
        DXGI_FORMAT_BC4_UNORM = 80,
        DXGI_FORMAT_BC4_SNORM = 81,
        DXGI_FORMAT_BC5_TYPELESS = 82,
        DXGI_FORMAT_BC5_UNORM = 83,
        DXGI_FORMAT_BC5_SNORM = 84,
        DXGI_FORMAT_B5G6R5_UNORM = 85,
        DXGI_FORMAT_B5G5R5A1_UNORM = 86,
        DXGI_FORMAT_B8G8R8A8_UNORM = 87,
        DXGI_FORMAT_B8G8R8X8_UNORM = 88,
        DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
        DXGI_FORMAT_B8G8R8A8_TYPELESS = 90,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
        DXGI_FORMAT_B8G8R8X8_TYPELESS = 92,
        DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93,
        DXGI_FORMAT_BC6H_TYPELESS = 94,
        DXGI_FORMAT_BC6H_UF16 = 95,
        DXGI_FORMAT_BC6H_SF16 = 96,
        DXGI_FORMAT_BC7_TYPELESS = 97,
        DXGI_FORMAT_BC7_UNORM = 98,
        DXGI_FORMAT_BC7_UNORM_SRGB = 99,
        DXGI_FORMAT_AYUV = 100,
        DXGI_FORMAT_Y410 = 101,
        DXGI_FORMAT_Y416 = 102,
        DXGI_FORMAT_NV12 = 103,
        DXGI_FORMAT_P010 = 104,
        DXGI_FORMAT_P016 = 105,
        DXGI_FORMAT_420_OPAQUE = 106,
        DXGI_FORMAT_YUY2 = 107,
        DXGI_FORMAT_Y210 = 108,
        DXGI_FORMAT_Y216 = 109,
        DXGI_FORMAT_NV11 = 110,
        DXGI_FORMAT_AI44 = 111,
        DXGI_FORMAT_IA44 = 112,
        DXGI_FORMAT_P8 = 113,
        DXGI_FORMAT_A8P8 = 114,
        DXGI_FORMAT_B4G4R4A4_UNORM = 115,
        DXGI_FORMAT_P208 = 130,
        DXGI_FORMAT_V208 = 131,
        DXGI_FORMAT_V408 = 132,
        DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE = 189,
        DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE = 190,
        DXGI_FORMAT_FORCE_UINT = 0xffffffff
    };
    enum D3D10_RESOURCE_DIMENSION
    {
        D3D10_RESOURCE_DIMENSION_UNKNOWN = 0,
        D3D10_RESOURCE_DIMENSION_BUFFER = 1,
        D3D10_RESOURCE_DIMENSION_TEXTURE1D = 2,
        D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3,
        D3D10_RESOURCE_DIMENSION_TEXTURE3D = 4
    };
    struct DDS_HEADER_DXT10
    {
        enum DXGI_FORMAT                dxgiFormat;
        enum D3D10_RESOURCE_DIMENSION   resourceDimension;
        UINT                            miscFlag;
        UINT                            arraySize;
        UINT                            miscFlags2;
    };
    struct DDS_PIXELFORMAT
    {
        DWORD dwSize;
        DWORD dwFlags;
        DWORD dwFourCC;
        DWORD dwRGBBitCount;
        DWORD dwRBitMask;
        DWORD dwGBitMask;
        DWORD dwBBitMask;
        DWORD dwABitMask;
    };
    struct DDS_HEADER
    {
        DWORD                   dwSize;
        DWORD                   dwFlags;
        DWORD                   dwHeight;
        DWORD                   dwWidth;
        DWORD                   dwPitchOrLinearSize;
        DWORD                   dwDepth;
        DWORD                   dwMipMapCount;
        DWORD                   dwReserved1[11];
        struct DDS_PIXELFORMAT  ddspf;
        DWORD                   dwCaps;
        DWORD                   dwCaps2;
        DWORD                   dwCaps3;
        DWORD                   dwCaps4;
        DWORD                   dwReserved2;
    };
    enum DDPF_FLAGS 
    {
        DDPF_ALPHAPIXELS =  0x1,
        DDPF_ALPHA =        0x2,
        DDPF_FOURCC =       0x4,
        DDPF_RGB =          0x40,
        DDPF_YUV =          0x200,
        DDPF_LUMINANCE =    0x20000,
    };
    enum DDSD_FLAGS
    {
        DDSD_CAPS =        0x1,
        DDSD_HEIGHT =      0x2,
        DDSD_WIDTH =       0x4,
        DDSD_PITCH =       0x8,
        DDSD_PIXELFORMAT = 0x1000,
        DDSD_MIPMAPCOUNT = 0x20000,
        DDSD_LINEARSIZE =  0x80000,
        DDSD_DEPTH =       0x800000,
    };
    enum DDS
    {
        DDS_FOURCC = 0x00000004,  // DDPF_FOURCC
        DDS_RGB = 0x00000040,  // DDPF_RGB
        DDS_RGBA = 0x00000041,  // DDPF_RGB | DDPF_ALPHAPIXELS
        DDS_LUMINANCE = 0x00020000,  // DDPF_LUMINANCE
        DDS_LUMINANCEA = 0x00020001,  // DDPF_LUMINANCE | DDPF_ALPHAPIXELS
        DDS_ALPHAPIXELS = 0x00000001,  // DDPF_ALPHAPIXELS
        DDS_ALPHA = 0x00000002,  // DDPF_ALPHA
        DDS_PAL8 = 0x00000020,  // DDPF_PALETTEINDEXED8
        DDS_PAL8A = 0x00000021,  // DDPF_PALETTEINDEXED8 | DDPF_ALPHAPIXELS
        DDS_BUMPLUMINANCE = 0x00040000,  // DDPF_BUMPLUMINANCE
        DDS_BUMPDUDV = 0x00080000,  // DDPF_BUMPDUDV
        DDS_BUMPDUDVA = 0x00080001  // DDPF_BUMPDUDV | DDPF_ALPHAPIXELS
    };

    DWORD* dwMagic = (DWORD*)buffer; dwMagic;
    buffer += sizeof(DWORD);
    if (!memcmp(dwMagic, "DDS ", 4) && false)
    {
        printf("Corrupted DDS file! dwMagic: %.*s\n", 4, (char*)&dwMagic);
        __debugbreak();
    }

    struct DDS_HEADER* header = (struct DDS_HEADER*)buffer;
    buffer += sizeof(struct DDS_HEADER);

    struct DDS_HEADER_DXT10* header10 = NULL;
    if (header->ddspf.dwFlags & DDPF_FOURCC && !memcmp(&header->ddspf.dwFourCC, "DX10", 4))
    {
        header10 = (struct DDS_HEADER_DXT10*)buffer;
        buffer += sizeof(struct DDS_HEADER_DXT10);
    }

    enum FORMAT texture_format = FORMAT_UNKNOWN;
    int pre_multiplied_alpha = 0;
    unsigned int texture_array_size = 1;

    if (header->ddspf.dwFlags & DDPF_FOURCC && !header10)
    {
        if (!memcmp(&header->ddspf.dwFourCC, "DXT1", 4))
            texture_format = FORMAT_BC1_UNORM;
        else if (!memcmp(&header->ddspf.dwFourCC, "DXT3", 4))
            texture_format = FORMAT_BC2_UNORM;
        else if (!memcmp(&header->ddspf.dwFourCC, "DXT5", 4))
            texture_format = FORMAT_BC3_UNORM;
        else if (!memcmp(&header->ddspf.dwFourCC, "BC4U", 4))
            texture_format = FORMAT_BC4_UNORM;
        else if (!memcmp(&header->ddspf.dwFourCC, "BC4S", 4))
            texture_format = FORMAT_BC4_SNORM;
        else if (!memcmp(&header->ddspf.dwFourCC, "ATI2", 4))
            texture_format = FORMAT_BC5_UNORM;
        else if (!memcmp(&header->ddspf.dwFourCC, "BC5S", 4))
            texture_format = FORMAT_BC5_SNORM;
        else if (!memcmp(&header->ddspf.dwFourCC, "RGBG", 4))
            texture_format = FORMAT_R8G8_B8G8_UNORM;
        else if (!memcmp(&header->ddspf.dwFourCC, "GRGB", 4))
            texture_format = FORMAT_G8R8_G8B8_UNORM;
        else if (header->ddspf.dwFourCC == 36)
            texture_format = FORMAT_R16G16B16A16_UNORM;
        else if (header->ddspf.dwFourCC == 110)
            texture_format = FORMAT_R16G16B16A16_SNORM;
        else if (header->ddspf.dwFourCC == 111)
            texture_format = FORMAT_R16_FLOAT;
        else if (header->ddspf.dwFourCC == 112)
            texture_format = FORMAT_R16G16_FLOAT;
        else if (header->ddspf.dwFourCC == 113)
            texture_format = FORMAT_R16G16B16A16_FLOAT;
        else if (header->ddspf.dwFourCC == 114)
            texture_format = FORMAT_R32_FLOAT;
        else if (header->ddspf.dwFourCC == 115)
            texture_format = FORMAT_R32G32_FLOAT;
        else if (header->ddspf.dwFourCC == 116)
            texture_format = FORMAT_R32G32B32A32_FLOAT;
        else if (!memcmp(&header->ddspf.dwFourCC, "DXT2", 4))
        {
            texture_format = FORMAT_BC1_UNORM;
            pre_multiplied_alpha = 1;
        }
        else if (!memcmp(&header->ddspf.dwFourCC, "DXT4", 4))
        {
            texture_format = FORMAT_BC2_UNORM;
            pre_multiplied_alpha = 1;
        }
        else if (!memcmp(&header->ddspf.dwFourCC, "UYVY", 4))
        {
            texture_format = FORMAT_R8G8_B8G8_UNORM;
            printf("Unsupported format!\n");
        }
        else if (!memcmp(&header->ddspf.dwFourCC, "YUY2", 4))
        {
            texture_format = FORMAT_G8R8_G8B8_UNORM;
            printf("Unsupported format!\n");
        }
        else if (header->ddspf.dwFourCC == 117)
        {
            texture_format = FORMAT_R8G8_SNORM;
            printf("Unsupported format!\n");
        }
        else
        {
            printf("Unsupported format!\n");
            __debugbreak();
        }
    }
    else if (!header10)
    {
        // Handle the pre-ddspf cluster fuck
        #define IS_FORMAT(_dwFlags, _dwRGBBitCount, _dwRBitMask, _dwGBitMask, _dwBBitMask, _dwABitMask) (header->ddspf.dwFlags == _dwFlags && header->ddspf.dwRGBBitCount == _dwRGBBitCount && header->ddspf.dwRBitMask == _dwRBitMask && header->ddspf.dwGBitMask == _dwGBitMask && header->ddspf.dwBBitMask == _dwBBitMask && header->ddspf.dwABitMask == _dwABitMask)
        if (IS_FORMAT(DDS_RGBA, 32, 0xff, 0xff00, 0xff0000, 0xff000000))
        {
            texture_format = FORMAT_R8G8B8A8_UNORM;
        }
        else if (IS_FORMAT(DDS_RGBA, 32, 0xffff, 0xffff0000, 0, 0))
        {
            texture_format = FORMAT_R16G16_UNORM;
        }
        else if (IS_FORMAT(DDS_RGBA, 32, 0x3ff, 0xffc00, 0x3ff00000, 0))
        {
            texture_format = FORMAT_R10G10B10A2_UNORM;
        }
        else if (IS_FORMAT(DDS_RGB, 32, 0xffff, 0xffff0000, 0, 0))
        {
            texture_format = FORMAT_R16G16_UNORM;
        }
        else if (IS_FORMAT(DDS_RGBA, 16, 0x7c00, 0x3e0, 0x1f, 0x8000))
        {
            texture_format = FORMAT_B5G5R5A1_UNORM;
        }
        else if (IS_FORMAT(DDS_RGB, 16, 0xf800, 0x7e0, 0x1f, 0))
        {
            texture_format = FORMAT_B5G6R5_UNORM;
        }
        else if (IS_FORMAT(DDS_ALPHA, 8, 0, 0, 0, 0xff))
        {
            texture_format = FORMAT_A8_UNORM;
        }
        else
        {
            printf("Unsupported format!\n");
            __debugbreak();
        }
        #undef IS_FORMAT
    }
    else
    {
        // Handle format shit from header10
        static enum FORMAT to_yara_format[DXGI_FORMAT_B4G4R4A4_UNORM+1] = {
            FORMAT_UNKNOWN,
            FORMAT_R32G32B32A32_TYPELESS,
            FORMAT_R32G32B32A32_FLOAT,
            FORMAT_R32G32B32A32_UINT,
            FORMAT_R32G32B32A32_SINT,
            FORMAT_R32G32B32_TYPELESS,
            FORMAT_R32G32B32_FLOAT,
            FORMAT_R32G32B32_UINT,
            FORMAT_R32G32B32_SINT,
            FORMAT_R16G16B16A16_TYPELESS,
            FORMAT_R16G16B16A16_FLOAT,
            FORMAT_R16G16B16A16_UNORM,
            FORMAT_R16G16B16A16_UINT,
            FORMAT_R16G16B16A16_SNORM,
            FORMAT_R16G16B16A16_SINT,
            FORMAT_R32G32_TYPELESS,
            FORMAT_R32G32_FLOAT,
            FORMAT_R32G32_UINT,
            FORMAT_R32G32_SINT,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_R10G10B10A2_TYPELESS,
            FORMAT_R10G10B10A2_UNORM,
            FORMAT_R10G10B10A2_UINT,
            FORMAT_R11G11B10_FLOAT,
            FORMAT_R8G8B8A8_TYPELESS,
            FORMAT_R8G8B8A8_UNORM,
            FORMAT_R8G8B8A8_UNORM_SRGB,
            FORMAT_R8G8B8A8_UINT,
            FORMAT_R8G8B8A8_SNORM,
            FORMAT_R8G8B8A8_SINT,
            FORMAT_R16G16_TYPELESS,
            FORMAT_R16G16_FLOAT,
            FORMAT_R16G16_UNORM,
            FORMAT_R16G16_UINT,
            FORMAT_R16G16_SNORM,
            FORMAT_R16G16_SINT,
            FORMAT_R32_TYPELESS,
            FORMAT_D32_FLOAT,
            FORMAT_R32_FLOAT,
            FORMAT_R32_UINT,
            FORMAT_R32_SINT,
            FORMAT_R24G8_TYPELESS,
            FORMAT_D24_UNORM_S8_UINT,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_R8G8_TYPELESS,
            FORMAT_R8G8_UNORM,
            FORMAT_R8G8_UINT,
            FORMAT_R8G8_SNORM,
            FORMAT_R8G8_SINT,
            FORMAT_R16_TYPELESS,
            FORMAT_R16_FLOAT,
            FORMAT_D16_UNORM,
            FORMAT_R16_UNORM,
            FORMAT_R16_UINT,
            FORMAT_R16_SNORM,
            FORMAT_R16_SINT,
            FORMAT_R8_TYPELESS,
            FORMAT_R8_UNORM,
            FORMAT_R8_UINT,
            FORMAT_R8_SNORM,
            FORMAT_R8_SINT,
            FORMAT_A8_UNORM,
            FORMAT_R1_UNORM,
            FORMAT_R9G9B9E5_SHAREDEXP,
            FORMAT_R8G8_B8G8_UNORM,
            FORMAT_G8R8_G8B8_UNORM,
            FORMAT_BC1_TYPELESS,
            FORMAT_BC1_UNORM,
            FORMAT_BC1_UNORM_SRGB,
            FORMAT_BC2_TYPELESS,
            FORMAT_BC2_UNORM,
            FORMAT_BC2_UNORM_SRGB,
            FORMAT_BC3_TYPELESS,
            FORMAT_BC3_UNORM,
            FORMAT_BC3_UNORM_SRGB,
            FORMAT_BC4_TYPELESS,
            FORMAT_BC4_UNORM,
            FORMAT_BC4_SNORM,
            FORMAT_BC5_TYPELESS,
            FORMAT_BC5_UNORM,
            FORMAT_BC5_SNORM,
            FORMAT_B5G6R5_UNORM,
            FORMAT_B5G5R5A1_UNORM,
            FORMAT_B8G8R8A8_UNORM,
            FORMAT_B8G8R8X8_UNORM,
            FORMAT_UNKNOWN,
            FORMAT_B8G8R8A8_TYPELESS,
            FORMAT_B8G8R8A8_UNORM_SRGB,
            FORMAT_B8G8R8X8_TYPELESS,
            FORMAT_B8G8R8X8_UNORM_SRGB,
            FORMAT_BC6H_TYPELESS,
            FORMAT_BC6H_UF16,
            FORMAT_BC6H_SF16,
            FORMAT_BC7_TYPELESS,
            FORMAT_BC7_UNORM,
            FORMAT_BC7_UNORM_SRGB,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_UNKNOWN,
            FORMAT_B4G4R4A4_UNORM
        };
        texture_format = to_yara_format[header10->dxgiFormat];
        texture_array_size = header10->arraySize;
    }
    
    uint8_t* image_data = (uint8_t*)buffer; // After header

    unsigned int mip_count = (header->dwFlags & DDSD_MIPMAPCOUNT) ? header->dwMipMapCount : 1;

    struct Buffer_Descriptor buffer_desc = {0};
    buffer_desc.width = (unsigned long long)(((header->dwWidth) + (4) - 1) & ~((4) - 1));
    buffer_desc.height = (unsigned long long)(((header->dwHeight) + (4) - 1) & ~((4) - 1));
    buffer_desc.mip_count = mip_count;
    buffer_desc.format = texture_format;
    buffer_desc.buffer_type = BUFFER_TYPE_TEXTRUE2D;
    buffer_desc.bind_types[0] = BIND_TYPE_SRV;
    buffer_desc.bind_types_count = 1;
    device_create_buffer(device, buffer_desc, &texture->buffer);
    buffer_set_name(texture->buffer, texture->path);
    device_create_shader_resource_view(device, 0, cbv_srv_uav_descriptor_set, texture->buffer, &texture->srv);

    struct Allocation_Info buffer_allocation_info = device_get_allocation_info(device, buffer_desc);

    struct Upload_Buffer* texture_upload_buffer = 0;
    device_create_upload_buffer(device, 0, buffer_allocation_info.size, &texture_upload_buffer);

    uint8_t* mapped_ptr = upload_buffer_map(texture_upload_buffer);
    size_t offset = 0;
    for (unsigned int array_element = 0; array_element < texture_array_size; array_element++)
    {
        for (unsigned int mip = 0; mip < mip_count; ++mip)
        {
            int mip_width = max(1, header->dwWidth >> mip);
            int mip_height = max(1, header->dwHeight >> mip);
            size_t mip_size = format_compute_mip_size(texture_format, mip_width, mip_height);
            memcpy(mapped_ptr + offset, image_data + offset, mip_size);
            offset  += mip_size;

            printf("Mip level: %d\n", mip);
            printf("Mip width: %d\n", mip_width);
            printf("Mip height: %d\n", mip_height);
            printf("Mip size: %zd\n", mip_size);
        }
    }
    upload_buffer_unmap(texture_upload_buffer);

    command_list_copy_upload_buffer_to_buffer(upload_command_list, texture_upload_buffer, texture->buffer);
    upload_buffer_destroy(texture_upload_buffer);
}

void load_texture(struct Texture *texture, struct Device *device, struct Descriptor_Set *cbv_srv_uav_descriptor_set, struct Command_List *upload_command_list)
{
    size_t path_len = strlen(texture->path);
    if (path_len <= 4)
        return;
    
    char* extension = texture->path + (path_len - 4);
    if (strcmp(extension, ".png") == 0)
        load_texture_png(texture, device, cbv_srv_uav_descriptor_set, upload_command_list);
    else if (strcmp(extension, ".dds") == 0)
        load_texture_dds(texture, device, cbv_srv_uav_descriptor_set, upload_command_list);
}

void upload_node_buffers(struct Node *node, struct Device *device, struct Command_List *upload_command_list, struct Descriptor_Set *cbv_srv_uav_descriptor_set)
{
    if (node->type == NODE_TYPE_MESH)
    {
        for (size_t i = 0; i < node->mesh.mesh_parts_count; i++)
        {
            struct Mesh_Part* mesh_part = &node->mesh.mesh_parts[i];

            struct Vertex* vertex_array = mesh_part->vertex_array;
            size_t vertex_count = mesh_part->vertex_count;
            unsigned int* index_array = mesh_part->index_array;
            size_t index_count = mesh_part->index_count;

            if (vertex_count == 0 || index_count == 0)
                continue;

            struct Upload_Buffer* vertex_upload_buffer = 0;
            device_create_upload_buffer(device, vertex_array, sizeof(struct Vertex) * vertex_count, &vertex_upload_buffer);
            {
                struct Buffer_Descriptor buffer_description = {
                    .width = sizeof(struct Vertex) * vertex_count,
                    .height = 1,
                    .buffer_type = BUFFER_TYPE_BUFFER,
                };
                device_create_buffer(device, buffer_description, &mesh_part->vertex_buffer);
            }

            struct Upload_Buffer* index_upload_buffer = 0;
            device_create_upload_buffer(device, index_array, sizeof(unsigned int) * index_count, &index_upload_buffer);
            {
                struct Buffer_Descriptor buffer_description = {
                    .width = sizeof(unsigned int) * index_count,
                    .height = 1,
                    .buffer_type = BUFFER_TYPE_BUFFER,
                };
                device_create_buffer(device, buffer_description, &mesh_part->index_buffer);
            }

            struct Model_Constant
            {
                Mat4 model_to_world;
                unsigned int enabled_color_texture;
                unsigned int enabled_normal_texture;
                unsigned int enabled_roughness_texture;
                unsigned int enabled_metallic_texture;
            };
            {
                struct Buffer_Descriptor buffer_description = {
                    .width = sizeof(struct Model_Constant),
                    .height = 1,
                    .buffer_type = BUFFER_TYPE_BUFFER,
                    .bind_types = {
                        BIND_TYPE_CBV
                    },
                    .bind_types_count = 1
                };
                device_create_buffer(device, buffer_description, &mesh_part->constant_buffer);

                device_create_constant_buffer_view(device, 0, cbv_srv_uav_descriptor_set, mesh_part->constant_buffer, &mesh_part->cbv);
            }

            struct Upload_Buffer* constant_upload_buffer = 0;
            {
                struct Model_Constant constant = { 
                    .model_to_world = node_global_transform_geometry(node),
                    .enabled_color_texture = mesh_part->color_texture != 0,
                    .enabled_normal_texture = mesh_part->normal_texture != 0,
                    .enabled_roughness_texture = 0,
                    .enabled_metallic_texture = 0,
                };
                device_create_upload_buffer(device, &constant, sizeof(struct Model_Constant), &constant_upload_buffer);
            }
            
            
            command_list_copy_upload_buffer_to_buffer(upload_command_list, vertex_upload_buffer, mesh_part->vertex_buffer);
            command_list_copy_upload_buffer_to_buffer(upload_command_list, index_upload_buffer, mesh_part->index_buffer);
            command_list_copy_upload_buffer_to_buffer(upload_command_list, constant_upload_buffer, mesh_part->constant_buffer);

            upload_buffer_destroy(vertex_upload_buffer);
            upload_buffer_destroy(index_upload_buffer);
            upload_buffer_destroy(constant_upload_buffer);
        }
    }

    for (size_t i = 0; i < node->child_count; i++)
    {
        upload_node_buffers(node->child_array[i], device, upload_command_list, cbv_srv_uav_descriptor_set);
    }

    if (!node->parent) // is_root
    {
        for (size_t i = 0; i < node->texture_count; i++)
        {
            struct Texture* texture = &node->texture_array[i];
            printf("Texture Path: %s\n", texture->path);

            load_texture(texture, device, cbv_srv_uav_descriptor_set, upload_command_list);
        }
    }
}

void draw_node(struct Node* node, struct Device* device, struct Command_List* command_list)
{
    if (node->type == NODE_TYPE_MESH)
    {
        for (size_t i = 0; i < node->mesh.mesh_parts_count; i++)
        {
            struct Mesh_Part* mesh_part = &node->mesh.mesh_parts[i];
            if (mesh_part->vertex_count == 0 || mesh_part->index_count == 0)
                continue;

            command_list_set_constant_buffer(command_list, mesh_part->cbv, 0);
            if (mesh_part->color_texture)
                command_list_set_texture_buffer(command_list, mesh_part->color_texture->srv, 5);
            if (mesh_part->normal_texture)
                command_list_set_texture_buffer(command_list, mesh_part->normal_texture->srv, 6);
            command_list_set_primitive_topology(command_list, PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list_set_vertex_buffer(command_list, mesh_part->vertex_buffer, sizeof(struct Vertex) * mesh_part->vertex_count, sizeof(struct Vertex));
            command_list_set_index_buffer(command_list, mesh_part->index_buffer, sizeof(unsigned int) * mesh_part->index_count, FORMAT_R32_UINT);
            command_list_draw_indexed_instanced(command_list, mesh_part->index_count, 1, 0, 0, 0);
        }
    }

    for (size_t i = 0; i < node->child_count; i++)
    {
        draw_node(node->child_array[i], device, command_list);
    }
}

void setup_shader_and_pso(struct Device* device, enum FORMAT swapchain_format, struct Shader** out_shader, struct Pipeline_State_Object** out_pipeline_state_object)
{
    if(device_create_shader(device, out_shader))
        return;

    struct Input_Element_Descriptor input_element_descriptors[] = {
        {
            .element_binding.name = "POS",
            .format = FORMAT_R32G32B32_FLOAT,
            .element_classification = INPUT_ELEMENT_CLASSIFICATION_PER_VERTEX,
        },
        {
            .element_binding.name = "COL",
            .format = FORMAT_R32G32B32A32_FLOAT,
            .element_classification = INPUT_ELEMENT_CLASSIFICATION_PER_VERTEX,
            .offset = offsetof(struct Vertex, color)
        },
        {
            .element_binding.name = "NORMAL",
            .format = FORMAT_R32G32B32_FLOAT,
            .element_classification = INPUT_ELEMENT_CLASSIFICATION_PER_VERTEX,
            .offset = offsetof(struct Vertex, normal)
        },
        {
            .element_binding.name = "TANGENT",
            .format = FORMAT_R32G32B32A32_FLOAT,
            .element_classification = INPUT_ELEMENT_CLASSIFICATION_PER_VERTEX,
            .offset = offsetof(struct Vertex, tangent)
        },
        {
            .element_binding.name = "UV",
            .format = FORMAT_R32G32_FLOAT,
            .element_classification = INPUT_ELEMENT_CLASSIFICATION_PER_VERTEX,
            .offset = offsetof(struct Vertex, uv)
        },
    };
    struct Pipeline_State_Object_Descriptor pipeline_state_object_descriptor = {
        .shader = *out_shader,
        .blend_descriptor.alpha_to_coverage_enable = 0,
        .blend_descriptor.independent_blend_enable = 0,
        .sample_mask = UINT_MAX,
        .rasterizer_descriptor.fill_mode = FILL_MODE_SOLID,
        .rasterizer_descriptor.cull_mode = CULL_MODE_BACK,
        .rasterizer_descriptor.front_counter_clockwise = 0,
        .depth_stencil_descriptor.stencil_enable = 0,
        .depth_stencil_descriptor.depth_enable = 1,
        .depth_stencil_descriptor.depth_func = COMPARISON_FUNC_LESS,
        .depth_stencil_descriptor.depth_write_mask = DEPTH_WRITE_MASK_ALL,
        .depth_stencil_descriptor.front_face_op.stencil_func = COMPARISON_FUNC_ALWAYS,
        .depth_stencil_descriptor.front_face_op.stencil_depth_fail_op = STENCIL_OP_KEEP,
        .depth_stencil_descriptor.front_face_op.stencil_fail_op = STENCIL_OP_KEEP,
        .depth_stencil_descriptor.front_face_op.stencil_pass_op = STENCIL_OP_KEEP,
        .depth_stencil_descriptor.back_face_op.stencil_func = COMPARISON_FUNC_ALWAYS,
        .depth_stencil_descriptor.back_face_op.stencil_depth_fail_op = STENCIL_OP_KEEP,
        .depth_stencil_descriptor.back_face_op.stencil_fail_op = STENCIL_OP_KEEP,
        .depth_stencil_descriptor.back_face_op.stencil_pass_op = STENCIL_OP_KEEP,
        .input_element_descriptors = input_element_descriptors,
        .input_element_descriptors_count = ARRAYSIZE(input_element_descriptors),
        .primitive_topology_type = PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .render_target_count = 1,
        .render_target_formats[0] = swapchain_format,
        .depth_stencil_format = FORMAT_D24_UNORM_S8_UINT,
        .sample_descriptor = {
            .count = 1,
            .quality = 0,
        }
    };
    for (int i = 0; i < 8; ++i)
    {
        pipeline_state_object_descriptor.blend_descriptor.render_target_blend_descriptors[i] = (struct Render_Target_Blend_Descriptor){
            .blend_enable = 1,
            .logic_op_enable = 0,
            .src_blend_type = BLEND_TYPE_SRC_ALPHA,
            .src_blend_type_alpha = BLEND_TYPE_ONE,
            .dest_blend_type = BLEND_TYPE_INV_SRC_ALPHA,
            .blend_op = BLEND_OP_ADD,
            .logic_op = LOGIC_OP_NOOP,
            .dest_blend_type_alpha = BLEND_TYPE_INV_SRC_ALPHA,
            .blend_op_alpha = BLEND_OP_ADD,
            .render_target_write_mask = 0x0F
        };
    }
    device_create_pipeline_state_object(device, pipeline_state_object_descriptor, out_pipeline_state_object);
}

int CALLBACK WinMain(HINSTANCE CurrentInstance, HINSTANCE PrevInstance, LPSTR CommandLine, int ShowCode)
{
    SetCpuAndThreadPriority();
    CreateConsole();

    CurrentInstance; PrevInstance; CommandLine; ShowCode;
    printf("Hello World!\n");

    // Windows
    WNDCLASSA WindowClass = {
        .lpfnWndProc = WindowCallback,
        .hInstance = CurrentInstance,
        .hCursor = LoadCursor(0, IDC_ARROW),
        .lpszClassName = "YaraWindowClass"
    };
    RegisterClassA(&WindowClass);
    HWND Window = CreateWindowExA(0, WindowClass.lpszClassName, "Yara", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, CurrentInstance, 0);

    // D3D12
    struct Device* device = 0;
    device_create(&device);

    struct Command_Queue* command_queue = 0;
    device_create_command_queue(device, &command_queue);

    struct Swapchain* swapchain = 0;
    device_create_swapchain(device, command_queue, (struct Swapchain_Descriptor){ .window = Window, .backbuffer_count = 2 }, &swapchain);

    struct Command_List* command_list = 0;
    device_create_command_list(device, &command_list);

    struct Descriptor_Set* rtv_descriptor_set = 0;
    device_create_descriptor_set(device, DESCRIPTOR_TYPE_RTV, 2048, &rtv_descriptor_set);

    struct Descriptor_Set* cbv_srv_uav_descriptor_set = 0;
    device_create_descriptor_set(device, DESCRIPTOR_TYPE_CBV_SRV_UAV, 2048, &cbv_srv_uav_descriptor_set);

    struct Descriptor_Set* dsv_descriptor_set = 0;
    device_create_descriptor_set(device, DESCRIPTOR_TYPE_DSV, 2048, &dsv_descriptor_set);

    struct Swapchain_Descriptor swapchain_descriptor = swapchain_get_descriptor(swapchain);
    struct Render_Target_View** backbuffers = _alloca(sizeof(struct Render_Target_View*) * swapchain_descriptor.backbuffer_count);
    swapchain_create_backbuffers(swapchain, device, rtv_descriptor_set, backbuffers);

    struct Buffer** depth_buffers = _alloca(sizeof(struct Buffer*) * swapchain_descriptor.backbuffer_count);
    struct Depth_Stencil_View** depth_stencil_views = _alloca(sizeof(struct Depth_Stencil_View*) * swapchain_descriptor.backbuffer_count);
    for (size_t i = 0; i < swapchain_descriptor.backbuffer_count; i++)
    {
        struct Buffer* depth_buffer = 0;
        struct Buffer_Descriptor buffer_description = {
            .width = swapchain_descriptor.width,
            .height = swapchain_descriptor.height,
            .buffer_type = BUFFER_TYPE_TEXTRUE2D,
            .bind_types = {
                BIND_TYPE_DSV
            },
            .bind_types_count = 1,
            .format = FORMAT_D24_UNORM_S8_UINT,
        };
        device_create_buffer(device, buffer_description, &depth_buffer);
        depth_buffers[i] = depth_buffer;

        device_create_depth_stencil_view(device, 0, dsv_descriptor_set, depth_buffer, &depth_stencil_views[i]);
    }
    
    struct Shader* shader = 0;
    struct Pipeline_State_Object* pipeline_state_object = 0;
    setup_shader_and_pso(device, swapchain_descriptor.format, &shader, &pipeline_state_object);

    #pragma pack(push, 1)
    struct Main_Constant
    {
        Mat4 world_to_clip;
        Vec3 camera_position;
        unsigned int lights;
    };
    #pragma pack(pop)
    struct Buffer* camera_constant_buffer = 0;
    struct Constant_Buffer_View* camera_cbv = 0;
    {
        struct Buffer_Descriptor buffer_description = {
            .width = sizeof(struct Main_Constant),
            .height = 1,
            .buffer_type = BUFFER_TYPE_BUFFER,
            .bind_types = {
                BIND_TYPE_CBV
            },
            .bind_types_count = 1
        };
        device_create_buffer(device, buffer_description, &camera_constant_buffer);

        device_create_constant_buffer_view(device, 0, cbv_srv_uav_descriptor_set, camera_constant_buffer, &camera_cbv);
    }

    enum LIGHT_TYPE
    {
        LIGHT_TYPE_POINT,
        LIGHT_TYPE_SPOT,
        LIGHT_TYPE_DIRECTIONAL
    };
    #pragma pack(push, 1)
    struct Light_Info
    {
        enum LIGHT_TYPE type;
        float pad0;
        float pad1;
        float pad2;
        Vec4 color;
        Vec3 pos;
        float radius;
        Vec3 dir;
        float inner_cone_angle;
        float outer_cone_angle;
        float pad3;
        float pad4;
        float pad5;
    };
    #pragma pack(pop)
    struct Light_Info lights[8] = {{
        .type = LIGHT_TYPE_DIRECTIONAL,
        .color = V4(1.0f, 1.0f, 1.0f, 1.0f),
        .dir = NormV3(V3(1.0f, -1.0f, -1.0f))
    }};
    struct Buffer* light_buffer = 0;
    struct Shader_Resource_View* light_srv = 0;
    {
        struct Buffer_Descriptor buffer_description = {
            .width = sizeof(struct Light_Info) * ARRAYSIZE(lights),
            .height = 1,
            .buffer_type = BUFFER_TYPE_BUFFER,
            .bind_types = {
                BIND_TYPE_SRV
            },
            .bind_types_count = 1
        };
        device_create_buffer(device, buffer_description, &light_buffer);

        struct Shader_Resource_View_Descriptor srv_desc = {
            .buffer_type = BUFFER_TYPE_BUFFER,
            .buffer_info = {
                .buffer = {
                    .element_count = ARRAYSIZE(lights),
                    .element_stride_bytes = sizeof(struct Light_Info)
                }
            }
        };
        device_create_shader_resource_view(device, &srv_desc, cbv_srv_uav_descriptor_set, light_buffer, &light_srv);
    }

    struct Buffer* eo_lut_buffer = 0;
    struct Shader_Resource_View* eo_lut_srv = 0;
    {
        struct Buffer_Descriptor buffer_description = {
            .width = 32,
            .height = 32,
            .buffer_type = BUFFER_TYPE_TEXTRUE2D,
            .format = FORMAT_R16_FLOAT,
            .bind_types = {
                BIND_TYPE_SRV
            },
            .bind_types_count = 1
        };
        device_create_buffer(device, buffer_description, &eo_lut_buffer);
        device_create_shader_resource_view(device, 0, cbv_srv_uav_descriptor_set, eo_lut_buffer, &eo_lut_srv);
    }

    struct Buffer* eavg_lut_buffer = 0;
    struct Shader_Resource_View* eavg_lut_srv = 0;
    {
        struct Buffer_Descriptor buffer_description = {
            .width = 32,
            .height = 1,
            .buffer_type = BUFFER_TYPE_TEXTRUE2D,
            .format = FORMAT_R16_FLOAT,
            .bind_types = {
                BIND_TYPE_SRV
            },
            .bind_types_count = 1
        };
        device_create_buffer(device, buffer_description, &eavg_lut_buffer);
        device_create_shader_resource_view(device, 0, cbv_srv_uav_descriptor_set, eavg_lut_buffer, &eavg_lut_srv);
    }
    
    #define BISTRO
    #ifdef BISTRO
    char* asset_path = get_asset_path("BistroExterior.fbx");
    #else
    char* asset_path = get_asset_path("Sphere.fbx");
    #endif
    struct Node* scene_node = load_fbx(asset_path);
    scene_node->local_scale = V3(0.5f, 0.5f, 0.5f);
    free(asset_path);

    {
        struct Command_List* upload_command_list = 0;
        device_create_command_list(device, &upload_command_list);

        command_list_reset(upload_command_list);

        upload_node_buffers(scene_node, device, upload_command_list, cbv_srv_uav_descriptor_set);

        // Load eo_lut
        {
            FILE* file = fopen("Eo.r16f", "rb");
            if (!file)
                __debugbreak();

            fseek(file, 0L, SEEK_END);
            size_t file_size = ftell(file);
            fseek(file, 0L, SEEK_SET);

            char* buffer = malloc(file_size);
            fread(buffer, 1, file_size, file);
            fclose(file);

            void* eo_lut_buffer_ptr = command_list_map_buffer(upload_command_list, eo_lut_buffer);
            memcpy(eo_lut_buffer_ptr, buffer, file_size);
            command_list_unmap_buffer(upload_command_list, eo_lut_buffer);
            free(buffer);
        }

        // Load eavg_lut
        {
            FILE* file = fopen("Eavg.r16f", "rb");
            if (!file)
                __debugbreak();

            fseek(file, 0L, SEEK_END);
            size_t file_size = ftell(file);
            fseek(file, 0L, SEEK_SET);

            char* buffer = malloc(file_size);
            fread(buffer, 1, file_size, file);
            fclose(file);

            void* eavg_lut_buffer_ptr = command_list_map_buffer(upload_command_list, eavg_lut_buffer);
            memcpy(eavg_lut_buffer_ptr, buffer, file_size);
            command_list_unmap_buffer(upload_command_list, eavg_lut_buffer);
            free(buffer);
        }

        command_list_close(upload_command_list);

        command_queue_execute(command_queue, &upload_command_list, 1);
    }
    
    #ifdef BISTRO
    Vec3 camera_position = { -3.38430858f, 2.55671954f, -1.69763803f };
    float camera_yaw = -47.7283516f;
    float camera_pitch = 21.6109619f;
    #else
    Vec3 camera_position = { 0.0f, 0.0f, 1.0f };
    float camera_yaw = 180.0f;
    float camera_pitch = 0.0f;
    #endif
    Mat4 camera_transform = M4D(1.0f);
    
    double frame_time_buffer[32] = {0};
    int frame_time_buffer_count = 0;
    double frame_time = 0.0f;
    unsigned long long frame_counter = 0;
    FILETIME lastWrite = {0};
    while (!DoneRunning)
    {
        WIN32_FILE_ATTRIBUTE_DATA attrs;
        GetFileAttributesExW(L"shader.hlsl", GetFileExInfoStandard, &attrs);
        if (CompareFileTime(&lastWrite, &attrs.ftLastWriteTime) != 0) {
            Sleep(100);
            setup_shader_and_pso(device, swapchain_descriptor.format, &shader, &pipeline_state_object);
            lastWrite = attrs.ftLastWriteTime;
            printf("reloaded shader\n");
        }

        unsigned long long timestamp1 = GetRdtsc();

        MSG Message;
        while (PeekMessageA(&Message, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessage(&Message);
        }

        if (keyboard_input['W'])
            camera_position = AddV3(camera_position, MulV3F(camera_transform.Columns[2].XYZ, 1.0f * (float)frame_time));
        if (keyboard_input['S'])
            camera_position = SubV3(camera_position, MulV3F(camera_transform.Columns[2].XYZ, 1.0f * (float)frame_time));
        if (keyboard_input['D'])
            camera_position = AddV3(camera_position, MulV3F(camera_transform.Columns[0].XYZ, 1.0f * (float)frame_time));
        if (keyboard_input['A'])
            camera_position = SubV3(camera_position, MulV3F(camera_transform.Columns[0].XYZ, 1.0f * (float)frame_time));
        if (keyboard_input['E'])
            camera_yaw += 40.0f * (float)frame_time;
        if (keyboard_input['Q'])
            camera_yaw -= 40.0f * (float)frame_time;
        if (keyboard_input['Z'])
            camera_pitch += 40.0f * (float)frame_time;
        if (keyboard_input['X'])
            camera_pitch -= 40.0f * (float)frame_time;

        int backbuffer_index = swapchain_get_current_backbuffer_index(swapchain);
        
        command_list_reset(command_list);
        
        struct Render_Target_View* backbuffer_rtv = backbuffers[backbuffer_index];
        struct Depth_Stencil_View* dsv = depth_stencil_views[backbuffer_index];
        struct Buffer_Descriptor backbuffer_description = buffer_get_descriptor(render_target_view_get_buffer(backbuffer_rtv));

        struct Viewport viewport = {
            .width = (float)backbuffer_description.width,
            .height = (float)backbuffer_description.height,
            .min_depth = 0.0f,
            .max_depth = 1.0
        };

        struct Rect scissor_rect = {
            .right = (long)backbuffer_description.width,
            .bottom = (long)backbuffer_description.height,
        };

        float clear_color[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        command_list_clear_render_target(command_list, backbuffer_rtv, clear_color);
        command_list_clear_depth_target(command_list, dsv, 1.0f, 0, 0);

        command_list_set_pipeline_state_object(command_list, pipeline_state_object);
        command_list_set_shader(command_list, shader);
        command_list_set_viewport(command_list, viewport);
        command_list_set_scissor_rect(command_list, scissor_rect);
        command_list_set_render_targets(command_list, &backbuffer_rtv, 1, dsv);
        command_list_set_descriptor_set(command_list, &cbv_srv_uav_descriptor_set, 1);
        
        {
            Mat4 camera_translation = Translate(camera_position);
            Mat4 camera_rotation_yaw = Rotate_RH(AngleDeg(camera_yaw), (Vec3){ 0.0f, 1.0f, 0.0f });
            Mat4 camera_rotation_pitch = Rotate_RH(AngleDeg(camera_pitch), (Vec3){ 1.0f, 0.0f, 0.0f });
            camera_transform = MulM4(camera_translation, MulM4(camera_rotation_yaw, camera_rotation_pitch));
            Mat4 camera_projection = Perspective_LH_ZO(AngleDeg(70.0f), 16.0f/9.0f, 0.1f, 1000.0f);
            struct Main_Constant constant = { 
                .world_to_clip = MulM4(camera_projection, InvGeneralM4(camera_transform)),
                .camera_position = camera_position,
                .lights = 1
            };
            struct Main_Constant* constant_buffer_ptr = command_list_map_buffer(command_list, camera_constant_buffer);
            *constant_buffer_ptr = constant;
            // memcpy(constant_buffer_ptr, &constant, sizeof(struct Main_Constant));
            command_list_unmap_buffer(command_list, camera_constant_buffer);
        }

        {
            struct Light_Info* light_buffer_ptr = command_list_map_buffer(command_list, light_buffer);
            memcpy(light_buffer_ptr, lights, sizeof(struct Light_Info) * ARRAYSIZE(lights));
            command_list_unmap_buffer(command_list, light_buffer);
        }

        command_list_set_texture_buffer(command_list, light_srv, 2);
        command_list_set_texture_buffer(command_list, eavg_lut_srv, 3);
        command_list_set_texture_buffer(command_list, eo_lut_srv, 4);
        command_list_set_constant_buffer(command_list, camera_cbv, 1);
        draw_node(scene_node, device, command_list);
        
        command_list_set_buffer_state(command_list, render_target_view_get_buffer(backbuffer_rtv), RESOURCE_STATE_PRESENT);
        command_list_close(command_list);

        command_queue_execute(command_queue, &command_list, 1);
        
        swapchain_present(swapchain);
        
        frame_counter++;

        unsigned long long timestamp2 = GetRdtsc();
        frame_time = (double)(timestamp2 - timestamp1) / GetRdtscFreq();
        // printf("ms: %f \r", frame_time * 1000.0);

        frame_time_buffer[frame_time_buffer_count++] = frame_time;
        if (frame_time_buffer_count >= ARRAYSIZE(frame_time_buffer)) 
        {
            double average_frame_time = 0.0;
            for (size_t i = 0; i < frame_time_buffer_count; i++)
            {
                average_frame_time += frame_time_buffer[i];
            }
            average_frame_time /= frame_time_buffer_count;
            printf("ms: %f \r", average_frame_time * 1000.0);
            frame_time_buffer_count = 0;
        }
    }
    
    return 0;
}