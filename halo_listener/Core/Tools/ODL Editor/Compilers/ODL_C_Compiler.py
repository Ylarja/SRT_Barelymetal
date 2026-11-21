#######################################################################################
# ODL C Compiler
# Version 1.0
#######################################################################################

data_objects = []
data_objects_folder_path = "../../../DOL"

#######################################################################################
# ODL Parser Module
#######################################################################################
import json
from collections import OrderedDict
from pathlib import Path
import time
import re
    
def Parse_ODL_File(filename):

    types_to_ctypes = {
        "int8": "int8_t", "uint8": "uint8_t",
        "int16": "int16_t", "uint16": "uint16_t",
        "int32": "int32_t", "uint32": "uint32_t",
        "int64": "int64_t", "uint64": "uint64_t",
        "char": "char", "float32": "float", "float64": "double",
    }

    C_IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

    C_KEYWORDS = {
        "int", "char", "float", "double", "struct", "typedef",
        "union", "enum", "static", "const", "void", "unsigned",
        "signed", "short", "long", "return", "if", "else", "for",
        "while", "do", "switch", "case", "break", "continue"
    }

    def Validate_C_Identifier(name: str, line: str):
        if not C_IDENTIFIER_RE.match(name):
            raise ValueError(
                f"Invalid identifier '{name}' in line:\n"
                f"    {line}"
            )
        if name in C_KEYWORDS:
            raise ValueError(
                f"Identifier '{name}' is a reserved C keyword in line:\n"
                f"    {line}"
            )


    with open(filename, "r") as f:
        lines = f.readlines()

    stack = []
    top_level_names = set()
    nested_structs_registry = {}  # struct_name -> template fields

    def fields_equal(f1, f2):
        if len(f1) != len(f2):
            return False
        for a, b in zip(f1, f2):
            if a["name"] != b["name"]:
                return False
            if a.get("ctype") != b.get("ctype"):
                return False
            if a.get("count", 1) != b.get("count", 1):
                return False
            # recursively check subfields
            if "fields" in a or "fields" in b:
                if "fields" not in a or "fields" not in b:
                    return False
                if not fields_equal(a["fields"], b["fields"]):
                    return False
        return True

    def register_nested_struct(field, raw_line):
        struct_name = field["name"].lower() + "_t"
        if struct_name not in nested_structs_registry:
            nested_structs_registry[struct_name] = field["fields"]
        else:
            if not fields_equal(nested_structs_registry[struct_name], field["fields"]):
                raise ValueError(
                    f"Nested struct '{struct_name}' conflict detected in line:\n    {raw_line}"
                )

    for raw_line in lines:
        line = raw_line.rstrip()
        if not line or line.strip().startswith("#"):
            continue

        indent = len(line) - len(line.lstrip())
        content = line.strip()

        if ":" not in content:
            raise ValueError(f"Invalid line: {line}")

        name_part, value_part = [s.strip() for s in content.split(":", 1)]
        Validate_C_Identifier(name_part, raw_line)

        # ---------------------
        # Top-level object
        if indent == 0:
            if name_part in top_level_names:
                raise ValueError(
                    f"Duplicate top-level object '{name_part}' in line:\n    {raw_line}"
                )
            top_level_names.add(name_part)

            obj = OrderedDict()
            obj["name"] = name_part
            obj["fields"] = []
            data_objects.append(obj)
            stack = [(indent, obj["fields"], None)]
            continue

        # ---------------------
        # Determine type and count
        if not value_part:
            type_ = None
            count = 1
        elif "," in value_part:
            type_, count_str = [v.strip() for v in value_part.split(",")]
            count = int(count_str)
        elif value_part.isdigit():
            type_ = None
            count = int(value_part)
        else:
            type_ = value_part
            count = 1

        Validate_C_Identifier(name_part, raw_line)

        # ---------------------
        # Create field dict
        if type_ is None:
            field = OrderedDict()
            field["name"] = name_part
            field["count"] = count
            field["fields"] = []
        elif type_ == "dobj":
            field = OrderedDict()
            field["name"] = name_part
            field["count"] = count
            field["is_dobj"] = True
        else:
            field = OrderedDict()
            field["name"] = name_part
            ctype = types_to_ctypes.get(type_)
            if ctype is None:
                raise ValueError(f"Unknown type: {type_} in line: {line}")
            field["ctype"] = ctype
            field["count"] = count

        # ---------------------
        # Pop stack elements that are no longer parents
        while stack and indent <= stack[-1][0]:
            _, _, fobj = stack.pop()
            if fobj and "fields" in fobj:
                register_nested_struct(fobj, raw_line)

        if not stack:
            raise ValueError(f"No parent found for line: {line}")

        parent_fields = stack[-1][1]
        parent_fields.append(field)

        # Push nested struct to stack to collect subfields
        if "fields" in field:
            stack.append((indent, field["fields"], field))

    # ---------------------
    # After reading all lines, register any remaining nested structs in the stack
    while stack:
        _, _, fobj = stack.pop()
        if fobj and "fields" in fobj:
            register_nested_struct(fobj, "<EOF>")


"""
def Parse_ODL_File(filename):
    with open(filename, "r") as f:
        lines = f.readlines()

    stack = []
    top_level_names = set()  # <-- keep track of top-level names

    for raw_line in lines:
        line = raw_line.rstrip()
        if not line or line.strip().startswith("#"):
            continue

        indent = len(line) - len(line.lstrip())
        content = line.strip()

        if ":" not in content:
            raise ValueError(f"Invalid line: {line}")

        name_part, value_part = [s.strip() for s in content.split(":", 1)]
        Validate_C_Identifier(name_part, raw_line)

        if indent == 0:
            if name_part in top_level_names:  # <-- check for duplicates
                raise ValueError(
                    f"Duplicate top-level object '{name_part}' in line:\n    {raw_line}"
                )
            top_level_names.add(name_part)

            obj = OrderedDict()
            obj["name"] = name_part
            obj["fields"] = []
            data_objects.append(obj)
            stack = [(indent, obj["fields"])]
            continue

        if not value_part:
            type = None
            count = 1
        elif "," in value_part:
            type, count_str = [v.strip() for v in value_part.split(",")]
            count = int(count_str)
        elif value_part.isdigit():
            type = None
            count = int(value_part)
        else:
            type = value_part
            count = 1

        Validate_C_Identifier(name_part, raw_line)

        if type is None:
            field = OrderedDict()
            field["name"] = name_part
            field["count"] = count
            field["fields"] = []

        elif type == "dobj":
            field = OrderedDict()
            field["name"] = name_part
            field["count"] = count
            field["is_dobj"] = True 

        else:
            field = OrderedDict()
            field["name"] = name_part

            ctype = types_to_ctypes.get(type)
            if ctype is None:
                raise ValueError(f"Unknown type: {type} in line: {line}")

            field["ctype"] = ctype
            field["count"] = count

        while stack and indent <= stack[-1][0]:
            stack.pop()
        if not stack:
            raise ValueError(f"No parent found for line: {line}")

        parent_list = stack[-1][1]
        parent_list.append(field)

        if type is None:
            stack.append((indent, field["fields"]))


"""

"""

def Parse_ODL_File(filename):
    # Same parser as before with OrderedDicts
    with open(filename, "r") as f:
        lines = f.readlines()

    stack = []

    for raw_line in lines:
        line = raw_line.rstrip()
        if not line or line.strip().startswith("#"):
            continue

        indent = len(line) - len(line.lstrip())
        content = line.strip()

        if ":" not in content:
            raise ValueError(f"Invalid line: {line}")

        name_part, value_part = [s.strip() for s in content.split(":", 1)]
        Validate_C_Identifier(name_part, raw_line)

        if indent == 0:
            obj = OrderedDict()
            obj["name"] = name_part
            #obj["type"] = value_part if value_part else name_part.lower() + "_t"
            obj["fields"] = []
            data_objects.append(obj)
            stack = [(indent, obj["fields"])]
            continue

        if not value_part:
            type = None
            count = 1
        elif "," in value_part:
            type, count_str = [v.strip() for v in value_part.split(",")]
            count = int(count_str)
        elif value_part.isdigit():
            type = None
            count = int(value_part)
        else:
            type = value_part
            count = 1

        Validate_C_Identifier(name_part, raw_line)

        if type is None:
            field = OrderedDict()
            field["name"] = name_part
            field["count"] = count
            field["fields"] = []

        elif type == "dobj":
            field = OrderedDict()
            field["name"] = name_part
            field["count"] = count
            field["is_dobj"] = True 

        else:
            field = OrderedDict()
            field["name"] = name_part

            ctype = types_to_ctypes.get(type)
            if ctype is None:
                raise ValueError(f"Unknown type: {type} in line: {line}")

            field["ctype"] = ctype
            field["count"] = count

        while stack and indent <= stack[-1][0]:
            stack.pop()
        if not stack:
            raise ValueError(f"No parent found for line: {line}")

        parent_list = stack[-1][1]
        parent_list.append(field)

        if type is None:
            stack.append((indent, field["fields"]))
"""
#######################################################################################
# C Code Generation Module
#######################################################################################

import os
#from collections import defaultdict
from pathlib import Path
#from Objects_Description import data_objects

ctype_sizes = {
    "int8_t": 1, "uint8_t": 1,
    "int16_t": 2, "uint16_t": 2,
    "int32_t": 4, "uint32_t": 4,
    "int64_t": 8, "uint64_t": 8,
    "char": 1, "float": 4, "double": 8,
}

# ----------------------------
def flatten_fields_helper(base_type, fields, parent_struct=None, parent_count=1, parent_offset=""):
    flat = []

    for field in fields:
        name = field["name"]

        # handle nested struct type
        if "fields" in field:
            struct_type = name.lower() + "_t"
            struct_count = field.get("count", 1)

            flat.extend(
                flatten_fields_helper(
                    struct_type,
                    field["fields"],
                    parent_struct=struct_type,
                    parent_count=struct_count,
                    parent_offset=(
                        f"offsetof({base_type}, {name})"
                        if not parent_offset
                        else f"{parent_offset} + offsetof({base_type}, {name})"
                    )
                )
            )
            continue  # done with this field

        # leaf field
        ctype = field["ctype"]

        if ctype not in ctype_sizes:
            raise ValueError(f"Unsupported ctype: {ctype}")

        count = field.get("count", 1)
        offset = (
            f"offsetof({base_type}, {name})"
            if not parent_offset
            else f"{parent_offset} + offsetof({base_type}, {name})"
        )

        is_struct_array = parent_struct is not None
        is_inner_array = count > 1 and is_struct_array

        if is_inner_array:
            inner_count = count
            element_count = parent_count * count
            stride = f"sizeof({parent_struct})"
        else:
            inner_count = 1
            element_count = parent_count * count
            stride = f"sizeof({parent_struct})" if is_struct_array else f"sizeof({ctype})"

        flat.append({
            "element_size": ctype,
            "element_count": element_count,
            "inner_count": inner_count,
            "offset": offset,
            "stride": stride
        })

    return flat


def Generate_C_Files(h_file, c_file):

    script_dir = Path(__file__).parent.resolve()
    output_h = script_dir / data_objects_folder_path / "Private" / h_file
    output_c = script_dir / data_objects_folder_path / "Src" / c_file

    # Clear existing files
    output_h.unlink(missing_ok=True)
    output_c.unlink(missing_ok=True)

    # ----------------------------
    # Generate .h
    # keep track of defined structs to avoid duplicates
    defined_structs = set()

    with open(output_h, "w") as f:
        f.write("// Auto-generated header\n#pragma once\n#include <stddef.h>\n#include <stdint.h>\n#include \"DOL_Types.h\"\n\n")

        f.write("#ifdef __cplusplus\n")
        f.write("extern \"C\" {\n")
        f.write("#endif\n\n")

        for obj in data_objects:
            # first generate nested structs
            for field in obj["fields"]:
                if "fields" in field:
                    struct_type = field["name"].lower() + "_t"
                    if struct_type not in defined_structs:
                        defined_structs.add(struct_type)
                        f.write(f"typedef struct {{\n")
                        for nf in field["fields"]:
                            count = nf.get("count", 1)
                            t = nf["ctype"]
                            n = nf["name"]
                            if count == 1:
                                f.write(f"    {t} {n};\n")
                            else:
                                f.write(f"    {t} {n}[{count}];\n")
                        f.write(f"}} {struct_type};\n\n")

            # now generate the main struct
            f.write(f"typedef struct {{\n")

            # generate the object header pointing to the descriptor
            f.write(f"    const object_desc_t* const descriptor;\n")

            for field in obj["fields"]:
                count = field.get("count", 1)
                n = field["name"]
                if "fields" in field:
                    t = field["name"].lower() + "_t"
                else:
                    t = field["ctype"]

                if count == 1:
                    f.write(f"    {t} {n};\n")
                else:
                    f.write(f"    {t} {n}[{count}];\n")
            f.write(f"}} {obj['name'].lower()}_t;\n\n")

        # extern descriptors
        for obj in data_objects:
            f.write(f"extern const object_desc_t {obj['name'].upper()}_DESCRIPTOR;\n")
        f.write(f"\n")
        
        # extern inits
        for obj in data_objects:
            f.write(f"extern const {obj['name'].lower()}_t {obj['name'].upper()}_INIT;\n")
        
        f.write("\n")
        f.write("#ifdef __cplusplus\n")
        f.write("}\n")
        f.write("#endif\n")

        f.close()

    # ----------------------------
    # Generate .c
    with open(output_c, "w") as f:
        f.write(f'#include "../Private/{os.path.basename(output_h)}"\n\n')

        for obj in data_objects:
            field_array_name = obj["name"].upper() + "_FIELDS_DESCRIPTOR"
            f.write(f"const field_desc_t {field_array_name}[] = {{\n")

            flat_fields = flatten_fields_helper(obj["name"].lower() + "_t", obj["fields"])
            for fd in flat_fields:
                f.write("    {\n")
                f.write(f"        .element_size = sizeof({fd['element_size']}),\n")
                f.write(f"        .element_count = {fd['element_count']},\n")
                f.write(f"        .inner_count = {fd['inner_count']},\n")
                f.write(f"        .offset = {fd['offset']},\n")
                f.write(f"        .stride = {fd['stride']}\n")
                f.write("    },\n")
            f.write("};\n\n")

            # compute total serialized size
            total_size = 0
            for fd in flat_fields:
                esize = ctype_sizes.get(fd['element_size'], 0)
                total_size += esize * fd['element_count']

            f.write(f"const object_desc_t {obj['name'].upper()}_DESCRIPTOR = {{\n")
            f.write(f"    .fields = {field_array_name},\n")
            f.write(f"    .field_count = sizeof({field_array_name}) / sizeof(field_desc_t),\n")
            f.write(f"    .message_size = {total_size},\n")
            f.write(f"    .object_size = sizeof({obj['name'].lower()}_t)\n")
            f.write("};\n\n")

            # Generate message init with descriptor pointer set
            f.write(f"const {obj['name'].lower()}_t {obj['name'].upper()}_INIT = {{\n")
            f.write(f"    .descriptor = &{obj['name'].upper()}_DESCRIPTOR,\n")
            f.write(f"}};\n\n")
        f.close()














def Generate_Public_Header():
    script_dir = Path(__file__).parent.resolve()

    private_dir = script_dir / data_objects_folder_path / "Private"
    include_dir = script_dir / data_objects_folder_path / "Include"

    include_dir.mkdir(parents=True, exist_ok=True)

    output_file = include_dir / "DOL.h"

    headers = sorted(
        h.name for h in private_dir.glob("*.h")
        if h.name != "DOL_Types.h"
    )

    with open(output_file, "w") as f:
        f.write("// Auto-generated public include file\n")
        f.write("#pragma once\n\n")
        f.write("#ifdef __cplusplus\n")
        f.write("extern \"C\" {\n")
        f.write("#endif\n\n")

        for h in headers:
            f.write(f'#include "../Private/{h}"\n')

        f.write("\n#ifdef __cplusplus\n")
        f.write("}\n")
        f.write("#endif\n")

def Migrate_Schema_File(schema_path: Path) -> Path:
    script_dir = Path(__file__).parent.resolve()
    schemas_dir = script_dir / data_objects_folder_path / "Schemas"
    schemas_dir.mkdir(parents=True, exist_ok=True)

    destination = schemas_dir / schema_path.name

    migrated = False
    if schema_path.resolve() != destination.resolve():
        schema_path.replace(destination)
        migrated = True

    return destination


############################################################################
# Compiler Entry Point
############################################################################

def Compile_ODL_Schema(schema_path: Path | None = None):
    if not schema_path.exists():
        raise FileNotFoundError(f"Schema file not found: {schema_path}")
    
    # Clear previous data objects (necessary for multiple calls in same session)
    data_objects.clear()

    Parse_ODL_File(schema_path)

    schema_name = schema_path.stem
    h_file = f"{schema_name}.h"
    c_file = f"{schema_name}.c"

    Generate_C_Files(h_file, c_file)

    Generate_Public_Header()

    new_schema_path = Migrate_Schema_File(schema_path)

    return {
        "schema_name": schema_name,
        "schema_path": new_schema_path,
        "code_generated": True,
        "schema_migrated": True
    }