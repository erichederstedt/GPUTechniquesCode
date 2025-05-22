#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "yara.h"

#include "util.h"
#include "HandmadeMath.h"

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

    struct Buffer** backbuffers = _alloca(sizeof(struct Buffer*) * swapchain_get_descriptor(swapchain).backbuffer_count);
    swapchain_create_backbuffers(swapchain, device, rtv_descriptor_set, backbuffers);
    
    struct Shader* shader = 0;
    device_create_shader(device, &shader);

    #pragma pack(push, 1)
    struct Vertex 
    {
        Vec3 pos;
        Vec4 color;
    };
    #pragma pack(pop)
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
    };
    struct Swapchain_Descriptor swapchain_descriptor = swapchain_get_descriptor(swapchain);
    struct Pipeline_State_Object_Descriptor pipeline_state_object_descriptor = {
        .shader = shader,
        .blend_descriptor.alpha_to_coverage_enable = 0,
        .blend_descriptor.independent_blend_enable = 0,
        .sample_mask = UINT_MAX,
        .rasterizer_descriptor.fill_mode = FILL_MODE_SOLID,
        .rasterizer_descriptor.cull_mode = CULL_MODE_BACK,
        .rasterizer_descriptor.front_counter_clockwise = 0,
        .depth_stencil_descriptor.depth_enable = 0,
        .depth_stencil_descriptor.stencil_enable = 0,
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

    struct Vertex vertices[] = {
        {.pos = { -0.4f, -0.5f, 0.0f }, .color = { 0.0f, 1.0f, 0.0f, 1.0f }},
        {.pos = { 0.0f, 0.7f, 0.0f },   .color = { 1.0f, 0.0f, 0.0f, 1.0f }},
        {.pos = { 0.4f, -0.5f, 0.0f },  .color = { 0.0f, 0.0f, 1.0f, 1.0f }},
    };
    struct Upload_Buffer* vertex_upload_buffer = 0;
    device_create_upload_buffer(device, vertices, sizeof(vertices), &vertex_upload_buffer);
    struct Buffer* vertex_buffer = 0;
    {
        struct Buffer_Descriptor buffer_description = {
            .width = sizeof(vertices),
            .height = 1,
            .descriptor_sets = {
                cbv_srv_uav_descriptor_set
            },
            .descriptor_sets_count = 1,
            .buffer_type = BUFFER_TYPE_BUFFER,
            .bind_types = {
                BIND_TYPE_SRV
            },
            .bind_types_count = 1
        };
        device_create_buffer(device, buffer_description, &vertex_buffer);
    }

    unsigned int indices[] = {
        0, 1, 2
    };  
    struct Upload_Buffer* index_upload_buffer = 0;
    device_create_upload_buffer(device, indices, sizeof(indices), &index_upload_buffer);
    struct Buffer* index_buffer = 0;
    {
        struct Buffer_Descriptor buffer_description = {
            .width = sizeof(indices),
            .height = 1,
            .descriptor_sets = {
                cbv_srv_uav_descriptor_set
            },
            .descriptor_sets_count = 1,
            .buffer_type = BUFFER_TYPE_BUFFER,
            .bind_types = {
                BIND_TYPE_SRV
            },
            .bind_types_count = 1
        };
        device_create_buffer(device, buffer_description, &index_buffer);
    }

    struct Constant
    {
        Mat4 model_to_world;
        Mat4 world_to_clip;
    };
    struct Buffer* constant_buffer = 0;
    {
        struct Buffer_Descriptor buffer_description = {
            .width = sizeof(struct Constant),
            .height = 1,
            .descriptor_sets = {
                cbv_srv_uav_descriptor_set
            },
            .descriptor_sets_count = 1,
            .buffer_type = BUFFER_TYPE_BUFFER,
            .bind_types = {
                BIND_TYPE_CBV
            },
            .bind_types_count = 1
        };
        device_create_buffer(device, buffer_description, &constant_buffer);
    }
        
    {
        struct Command_List* upload_command_list = 0;
        device_create_command_list(device, &upload_command_list);

        command_list_reset(upload_command_list);

        command_list_copy_upload_buffer_to_buffer(upload_command_list, vertex_upload_buffer, vertex_buffer);
        command_list_copy_upload_buffer_to_buffer(upload_command_list, index_upload_buffer, index_buffer);

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
        
        struct Buffer* backbuffer = backbuffers[backbuffer_index];
        struct Buffer_Descriptor backbuffer_description = buffer_get_descriptor(backbuffer);

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

        struct Upload_Buffer* constant_upload_buffer = 0;
        {
            Mat4 camera_translation = Translate(camera_position);
            Mat4 camera_rotation_yaw = Rotate_RH(AngleDeg(camera_yaw), (Vec3){ 0.0f, 1.0f, 0.0f });
            Mat4 camera_rotation_pitch = Rotate_RH(AngleDeg(camera_pitch), (Vec3){ 1.0f, 0.0f, 0.0f });
            camera_transform = MulM4(camera_translation, MulM4(camera_rotation_yaw, camera_rotation_pitch));
            Mat4 camera_projection = Perspective_LH_ZO(AngleDeg(70.0f), 16.0f/9.0f, 0.1f, 100.0f);
            struct Constant constant = { 
                .model_to_world = M4D(1.0f),
                .world_to_clip = MulM4(camera_projection, InvGeneralM4(camera_transform))
            };
            device_create_upload_buffer(device, &constant, sizeof(struct Constant), &constant_upload_buffer);
        }
        command_list_copy_upload_buffer_to_buffer(command_list, constant_upload_buffer, constant_buffer);
        
        command_list_set_pipeline_state_object(command_list, pipeline_state_object);
        command_list_set_shader(command_list, shader);
        command_list_set_viewport(command_list, viewport);
        command_list_set_scissor_rect(command_list, scissor_rect);
        
        command_list_set_render_targets(command_list, &backbuffer, 1, 0);
        float clear_color[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        command_list_clear_render_target(command_list, backbuffer, clear_color);
        command_list_set_vertex_buffer(command_list, vertex_buffer, sizeof(vertices), sizeof(struct Vertex));
        command_list_set_index_buffer(command_list, index_buffer, sizeof(indices), FORMAT_R32_UINT);
        command_list_set_constant_buffer(command_list, constant_buffer, 0);
        command_list_set_primitive_topology(command_list, PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_draw_indexed_instanced(command_list, 3, 1, 0, 0, 0);
        
        command_list_set_buffer_state(command_list, backbuffer, RESOURCE_STATE_PRESENT);
        command_list_close(command_list);

        command_queue_execute(command_queue, &command_list, 1);

        upload_buffer_destroy(constant_upload_buffer);
        
        swapchain_present(swapchain);
        
        // Sleep(2);
        frame_counter++;

        unsigned long long timestamp2 = GetRdtsc();
        frame_time = (double)(timestamp2 - timestamp1) / GetRdtscFreq();
        printf("ms: %f \r", frame_time * 1000.0);
    }
    
    return 0;
}