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

enum NODE_TYPE 
{
    NODE_TYPE_EMPTY,
    NODE_TYPE_MESH,
    NODE_TYPE_LIGHT_POINT,
    NODE_TYPE_LIGHT_SPOT,
    NODE_TYPE_LIGHT_DIRECTIONAL,
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
};
struct Node* node_create()
{
    struct Node* node = calloc(sizeof(struct Node), 1);
    return node;
}
void node_append_child(struct Node* node, struct Node* child);
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
};
struct Mesh_Part load_mesh_part(ufbx_mesh *mesh, ufbx_mesh_part *part)
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

    return mesh_part;
}
struct Node* load_node(ufbx_node* fbx_node)
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
            node->mesh.mesh_parts[i] = load_mesh_part(mesh, &mesh->material_parts.data[i]);
        }
    }
    else if (fbx_node->light && fbx_node->light->type == UFBX_LIGHT_POINT) {}
    else if (fbx_node->light && fbx_node->light->type == UFBX_LIGHT_SPOT) {}
    else if (fbx_node->light && fbx_node->light->type == UFBX_LIGHT_DIRECTIONAL) {}
    else 
    {
        node->type = NODE_TYPE_EMPTY;
    }

    node->child_array = calloc(fbx_node->children.count, sizeof(struct Node*));
    node->child_count = fbx_node->children.count;
    for (size_t i = 0; i < fbx_node->children.count; i++) 
    {
        node->child_array[i] = load_node(fbx_node->children.data[i]);
        node->child_array[i]->parent = node;
    }

    return node;
}
struct Node* load_fbx(char* path)
{
    ufbx_load_opts opts = { 0 };
    ufbx_error error;
    ufbx_scene *fbx_scene = ufbx_load_file(path, &opts, &error);
    if (!fbx_scene)
    {
        fprintf(stderr, "Failed to load: %s\n", error.description.data);
        exit(1);
    }

    struct Node* scene = load_node(fbx_scene->root_node);

    ufbx_free_scene(fbx_scene);
    return scene;
}

void upload_node_buffers(struct Node* node, struct Device* device, struct Command_List* upload_command_list, struct Descriptor_Set* cbv_srv_uav_descriptor_set)
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
                    .model_to_world = node_global_transform_geometry(node)
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
            .format = FORMAT_D24_UNORM_S8_UINT
        };
        device_create_buffer(device, buffer_description, &depth_buffer);
        depth_buffers[i] = depth_buffer;

        device_create_depth_stencil_view(device, 0, dsv_descriptor_set, depth_buffer, &depth_stencil_views[i]);
    }
    
    struct Shader* shader = 0;
    device_create_shader(device, &shader);

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
            .element_binding.name = "UV",
            .format = FORMAT_R32G32_FLOAT,
            .element_classification = INPUT_ELEMENT_CLASSIFICATION_PER_VERTEX,
            .offset = offsetof(struct Vertex, uv)
        },
    };
    struct Pipeline_State_Object_Descriptor pipeline_state_object_descriptor = {
        .shader = shader,
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
        .render_target_formats[0] = swapchain_descriptor.format,
        .depth_stencil_format = FORMAT_D24_UNORM_S8_UINT,
        .sample_descriptor = {
            .count = 1,
            .quality = 0,
        }
    };
    for (int i = 0; i < 8; ++i)
    {
        pipeline_state_object_descriptor.blend_descriptor.render_target_blend_descriptors[i] = (struct Render_Target_Blend_Descriptor){
            .blend_enable = 0,
            .logic_op_enable = 0,
            .render_target_write_mask = COLOR_WRITE_ENABLE_ALL
        };
    }
    struct Pipeline_State_Object* pipeline_state_object = 0;
    device_create_pipeline_state_object(device, pipeline_state_object_descriptor, &pipeline_state_object);

    struct Camera_Constant
    {
        Mat4 world_to_clip;
    };
    struct Buffer* camera_constant_buffer = 0;
    struct Constant_Buffer_View* camera_cbv = 0;
    {
        struct Buffer_Descriptor buffer_description = {
            .width = sizeof(struct Camera_Constant),
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
    
    struct Node* scene_node = load_fbx("Knight_USD_002.fbx");
    scene_node->local_position = V3(0.0f, 0.0f, 10.0f);
    scene_node->local_scale = V3(0.01f, 0.01f, 0.01f);

    {
        struct Command_List* upload_command_list = 0;
        device_create_command_list(device, &upload_command_list);

        command_list_reset(upload_command_list);

        upload_node_buffers(scene_node, device, upload_command_list, cbv_srv_uav_descriptor_set);

        command_list_close(upload_command_list);

        command_queue_execute(command_queue, &upload_command_list, 1);
    }
    
    Vec3 camera_position = { 0.0f, 0.0f, -1.0f };
    float camera_yaw = 0.0f;
    float camera_pitch = 0.0f;
    Mat4 camera_transform = M4D(1.0f);

    double frame_time = 0.0f;
    unsigned long long frame_counter = 0;
    while (!DoneRunning)
    {
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
        
        Mat4 camera_translation = Translate(camera_position);
        Mat4 camera_rotation_yaw = Rotate_RH(AngleDeg(camera_yaw), (Vec3){ 0.0f, 1.0f, 0.0f });
        Mat4 camera_rotation_pitch = Rotate_RH(AngleDeg(camera_pitch), (Vec3){ 1.0f, 0.0f, 0.0f });
        camera_transform = MulM4(camera_translation, MulM4(camera_rotation_yaw, camera_rotation_pitch));
        Mat4 camera_projection = Perspective_LH_ZO(AngleDeg(70.0f), 16.0f/9.0f, 0.1f, 100.0f);
        struct Upload_Buffer* constant_upload_buffer = 0;
        {
            struct Camera_Constant constant = { 
                .world_to_clip = MulM4(camera_projection, InvGeneralM4(camera_transform))
            };
            device_create_upload_buffer(device, &constant, sizeof(struct Camera_Constant), &constant_upload_buffer);
        }
        command_list_copy_upload_buffer_to_buffer(command_list, constant_upload_buffer, camera_constant_buffer);
        upload_buffer_destroy(constant_upload_buffer);
        
        command_list_set_constant_buffer(command_list, camera_cbv, 1);
        draw_node(scene_node, device, command_list);
        
        command_list_set_buffer_state(command_list, render_target_view_get_buffer(backbuffer_rtv), RESOURCE_STATE_PRESENT);
        command_list_close(command_list);

        command_queue_execute(command_queue, &command_list, 1);
        
        swapchain_present(swapchain);
        
        frame_counter++;

        unsigned long long timestamp2 = GetRdtsc();
        frame_time = (double)(timestamp2 - timestamp1) / GetRdtscFreq();
        printf("ms: %f \r", frame_time * 1000.0);
    }
    
    return 0;
}