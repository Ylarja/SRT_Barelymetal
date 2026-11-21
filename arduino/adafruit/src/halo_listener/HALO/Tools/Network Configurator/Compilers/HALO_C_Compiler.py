import json
from pathlib import Path

def compile_network_configuration(json_path):

    import os

    """
    Compile network configuration from JSON file.
    
    Args:
        json_path: Path to the JSON configuration file
    """
    # Load JSON configuration
    with open(json_path, 'r') as f:
        config = json.load(f)
    
    
    # Extract network model parameters from settings string or use defaults
    import math
    import re
    settings = config.get('settings', {})
    network_model_str = settings.get('network_model', '')
    sync_period_us = int(settings.get('sync_period_us', 1000000))
    
    # Default values
    num_nodes_model = 32
    num_channels_model = 32
    
    # Parse network model string if present (e.g., "32 nodes, 32 local channels")
    if network_model_str:
        # Extract number of nodes
        nodes_match = re.search(r'(\d+)\s+nodes?', network_model_str, re.IGNORECASE)
        if nodes_match:
            num_nodes_model = int(nodes_match.group(1))
        
        # Extract number of local channels
        channels_match = re.search(r'(\d+)\s+local\s+channels?', network_model_str, re.IGNORECASE)
        if channels_match:
            num_channels_model = int(channels_match.group(1))
    
    node_id_bits = math.ceil(math.log2(num_nodes_model)) if num_nodes_model > 1 else 1
    local_channel_id_bits = math.ceil(math.log2(num_channels_model)) if num_channels_model > 1 else 1
    
    # Generate HALO_Params.h file
    # JSON is in src/, Config is src/HALO/Config
    src_dir = Path(json_path).parent
    output_dir = Path(os.path.abspath(__file__)).parent.parent.parent.parent / "Config"
    header_path = output_dir / "Private" / "HALO_Params.h"
    header_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(header_path, 'w') as f:
        f.write("// Auto-generated HALO Network Configuration Parameters\n")
        f.write("// DO NOT EDIT MANUALLY\n\n")
        f.write("#ifndef HALO_PARAMS_H\n")
        f.write("#define HALO_PARAMS_H\n\n")
        f.write("#ifdef __cplusplus\n")
        f.write("extern \"C\" {\n")
        f.write("#endif\n\n")
        f.write(f"#define HALO_NETWORK_MODEL_NODE_ID_BITS {node_id_bits}\n")
        f.write(f"#define HALO_NETWORK_MODEL_LOCAL_CHANNEL_ID_BITS {local_channel_id_bits}\n")
        f.write(f"#define HALO_SYNC_PERIOD_US {sync_period_us}\n\n")
        f.write("#ifdef __cplusplus\n")
        f.write("}\n")
        f.write("#endif\n\n")
        f.write("#endif // HALO_PARAMS_H\n")
    
    print(f"Generated {header_path}")
    print(f"  - Network model: {network_model_str}")
    print(f"  - Node ID bits: {node_id_bits}")
    print(f"  - Local channel ID bits: {local_channel_id_bits}")
    print(f"  - Sync period: {sync_period_us} us")
    
    # Generate Channels.h and Channels.c files
    channels = config.get('channels', [])
    listener_names = {n.get('name') for n in config.get('nodes', []) if n.get('is_listener', False)}
    for ch in channels:
        if ch.get('controller') in listener_names or ch.get('responder') in listener_names:
            raise ValueError(f"Channel '{ch.get('name')}' uses a listener node as controller/responder")
    if listener_names and not channels:
        raise ValueError("A listener node requires at least one channel")
    messages_path = config.get('settings', {}).get('messages_path', '')
    
    # Resolve DOL Private headers directory and compute relative include path
    dol_private = Path(os.path.abspath(__file__)).parent.parent.parent.parent.parent / "Core" / "DOL" / "Private"
    dol_private = dol_private.resolve()
    dol_relative = os.path.relpath(dol_private, header_path.parent).replace('\\', '/')

    if messages_path == "All" or not messages_path:
        # Include all headers in DOL/Private (excluding DOL_Types.h)
        messages_includes = []
        if dol_private.is_dir():
            for h in sorted(dol_private.glob("*.h")):
                if h.name != "DOL_Types.h":
                    messages_includes.append(f'{dol_relative}/{h.name}')
    else:
        # Single header
        messages_includes = [f'{dol_relative}/{messages_path}']
    
    # Generate Channels.h (header with struct definitions and externs)
    channels_h_path = output_dir / "Private" / "HALO_Channels.h"
    channels_h_path.parent.mkdir(parents=True, exist_ok=True)
    with open(channels_h_path, 'w') as f:
        f.write("// Auto-generated HALO Channel Definitions\n")
        f.write("// DO NOT EDIT MANUALLY\n\n")
        f.write("#ifndef CHANNELS_H\n")
        f.write("#define CHANNELS_H\n\n")
        f.write("#ifdef __cplusplus\n")
        f.write("extern \"C\" {\n")
        f.write("#endif\n\n")
        f.write("#include <stdint.h>\n")
        f.write(f"#include \"HALO_Types.h\"\n")
        f.write(f"#include \"HALO_Nodes.h\"\n")
        for inc in messages_includes:
            f.write(f"#include \"{inc}\"\n")
        f.write("\n")

        
        # Write externs
        f.write("// Channel instances\n")
        for channel in channels:
            channel_name = "HALO_CHANNEL_" + channel.get('name', '').upper()
            f.write(f"extern halo_channel_t {channel_name};\n")
        
        f.write("\n#ifdef __cplusplus\n")
        f.write("}\n")
        f.write("#endif\n\n")
        f.write("#endif // CHANNELS_H\n")
    
    print(f"Generated {channels_h_path}")
    
    # Generate Channels.c (implementation with channel definitions)
    channels_c_path = output_dir / "Src" / "HALO_Channels.c"
    channels_c_path.parent.mkdir(parents=True, exist_ok=True)
    with open(channels_c_path, 'w') as f:
        f.write("// Auto-generated HALO Channel Implementations\n")
        f.write("// DO NOT EDIT MANUALLY\n\n")
        f.write("#include \"../Private/HALO_Channels.h\"\n\n")
        
        # Write channel definitions
        for channel in channels:
            channel_name = "HALO_CHANNEL_" + channel.get('name', '').upper()

            global_id = channel.get('global_id', '0')
            local_id = channel.get('local_id', '0')

            controller = channel.get('controller', '')
            responder = channel.get('responder', '')
            
            # Convert controller and responder names to node references
            controller_node = "HALO_NODE_" + controller.upper() if controller else "NULL"
            responder_node = "HALO_NODE_" + responder.upper() if responder else "NULL"
            
            lane0_msg = channel.get('lane0_message', {})
            lane0_msg_desc = lane0_msg.get('message_descriptor_name', '') if isinstance(lane0_msg, dict) else ''
            lane0_timeout = channel.get('lane0_rx_timeout', '0')
            
            lane1_msg = channel.get('lane1_message', {})
            lane1_msg_desc = lane1_msg.get('message_descriptor_name', '') if isinstance(lane1_msg, dict) else ''
            
            f.write(f"halo_channel_t {channel_name} = {{\n")
            f.write(f"  .name = \"{channel.get('name', '')}\",\n")
            f.write(f"  .global_id = {global_id},\n")
            f.write(f"  .local_id = {local_id},\n")
            f.write(f"  .controller = &{controller_node},\n" if controller else f"  .controller = NULL,\n")
            f.write(f"  .responder = &{responder_node},\n" if responder else f"  .responder = NULL,\n")
            f.write(f"  .lane0_message = &{lane0_msg_desc},\n")
            f.write(f"  .lane0_request_message = &HALO_REQUEST_MESSAGE_DESCRIPTOR,\n")
            f.write(f"  .lane0_rx_timeout = {lane0_timeout},\n")
            f.write(f"  .lane1_message = &{lane1_msg_desc}\n")
            f.write(f"}};\n\n")
    
    print(f"Generated {channels_c_path}")
    
    # Generate Nodes.h (header with node definitions and externs)
    nodes_h_path = output_dir / "Private" / "HALO_Nodes.h"
    nodes_h_path.parent.mkdir(parents=True, exist_ok=True)
    with open(nodes_h_path, 'w') as f:
        f.write("// Auto-generated HALO Node Definitions\n")
        f.write("// DO NOT EDIT MANUALLY\n\n")
        f.write("#ifndef HALO_NODES_H\n")
        f.write("#define HALO_NODES_H\n\n")
        f.write("#ifdef __cplusplus\n")
        f.write("extern \"C\" {\n")
        f.write("#endif\n\n")
        f.write("#include <stdint.h>\n")
        f.write("#include \"HALO_Params.h\"\n")
        f.write("#include \"HALO_Types.h\"\n")
        f.write(f"#include \"HALO_Channels.h\"\n\n")
        
        # Write externs
        f.write("// Node instances\n")
        for node in config.get('nodes', []):
            
            node_name = "HALO_NODE_" + node.get('name', '').upper()
            f.write(f"extern halo_node_t {node_name};\n")
        
        f.write("\n#ifdef __cplusplus\n")
        f.write("}\n")
        f.write("#endif\n\n")
        f.write("#endif // HALO_NODES_H\n")
    
    print(f"Generated {nodes_h_path}")
    
    # Generate Nodes.c (implementation with node definitions)
    nodes_c_path = output_dir / "Src" / "HALO_Nodes.c"
    nodes_c_path.parent.mkdir(parents=True, exist_ok=True)
    with open(nodes_c_path, 'w') as f:
        f.write("// Auto-generated HALO Node Implementations\n")
        f.write("// DO NOT EDIT MANUALLY\n\n")
        f.write("#include \"../Private/HALO_Nodes.h\"\n\n")
        
        # Write node definitions
        for node in config.get('nodes', []):
            node_name = "HALO_NODE_" + node.get('name', '').upper()
            node_id = node.get('node_id', '0')
            if node.get('is_listener', False):
                # Authoritative: derive from the channel list, not from GUI metrics
                channels_list = [c.get('name', '') for c in channels if c.get('name')]
                num_channels = len(channels_list)
                num_controller_endpoints = 0
                num_responder_endpoints = 0
            else:
                num_channels = node.get('channel_count', 0)
                num_controller_endpoints = node.get('controller_endpoints_count', 0)
                num_responder_endpoints = node.get('responder_endpoints_count', 0)
                channels_list = node.get('connected_channels', [])
            
            f.write(f"halo_node_t {node_name} = {{\n")
            f.write(f"  .name = \"{node.get('name', '')}\",\n")
            f.write(f"  .id = {node_id},\n")
            f.write(f"  .num_channels = {num_channels},\n")
            f.write(f"  .num_controller_endpoints = {num_controller_endpoints},\n")
            f.write(f"  .num_responder_endpoints = {num_responder_endpoints},\n")
            f.write(f"  .channels = {{ {', '.join([f'&HALO_CHANNEL_{c.upper()}' for c in channels_list])} }}\n")
            f.write(f"}};\n\n")
    
    print(f"Generated {nodes_c_path}")
    
    # Generate HALO_Config.h (controller header that includes all Private headers)
    config_h_path = output_dir / "Include" / "HALO_Config.h"
    config_h_path.parent.mkdir(parents=True, exist_ok=True)
    with open(config_h_path, 'w') as f:
        f.write("// Auto-generated HALO Configuration\n")
        f.write("// DO NOT EDIT MANUALLY\n\n")
        f.write("#ifndef HALO_CONFIG_H\n")
        f.write("#define HALO_CONFIG_H\n\n")
        f.write("#ifdef __cplusplus\n")
        f.write("extern \"C\" {\n")
        f.write("#endif\n\n")
        f.write("#include \"../Private/HALO_Params.h\"\n")
        f.write("#include \"../Private/HALO_Nodes.h\"\n")
        f.write("#include \"../Private/HALO_Channels.h\"\n\n")
        f.write("#ifdef __cplusplus\n")
        f.write("}\n")
        f.write("#endif\n\n")
        f.write("#endif // HALO_CONFIG_H\n")
    
    print(f"Generated {config_h_path}")
