#!/usr/bin/env python3
"""
OrcaSlicer Config Code Generator

Reads compiled protobuf descriptor set and generates C++ source files
that replace hand-written config registration, preset lists, and invalidation chains.

Usage:
    # Step 1: Compile .proto files to a descriptor set
    protoc --proto_path=src/PrintConfigs --descriptor_set_out=config.desc \
           --include_imports src/PrintConfigs/*.proto

    # Step 2: Generate Python bindings (one-time, or when config_metadata.proto changes)
    protoc --proto_path=src/PrintConfigs --python_out=tools/ config_metadata.proto

    # Step 3: Run codegen
    python tools/config_codegen.py config.desc codegen/generated/

Outputs:
    - PrintConfigDef_generated.cpp  (init_fff_params body)
    - Preset_options_generated.cpp  (s_Preset_*_options arrays)
    - Invalidation_generated.cpp    (opt_key -> steps map)
    - OptionKeys_generated.cpp      (extruder/filament key lists)
"""

import sys
import os
import argparse
from pathlib import Path

# Add tools/ to path so we can import generated config_metadata_pb2
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    from google.protobuf import descriptor_pb2
    # Import the generated bindings - this registers extensions globally
    import config_metadata_pb2 as meta_pb2
except ImportError as e:
    print(f"ERROR: {e}")
    print("Ensure google-protobuf is installed: pip install protobuf")
    print("And that config_metadata_pb2.py exists in tools/")
    print("Generate it with: protoc --proto_path=src/PrintConfigs --python_out=tools/ config_metadata.proto")
    sys.exit(1)


# Proto FieldDescriptorProto.Type enum values
TYPE_DOUBLE   = 1
TYPE_FLOAT    = 2
TYPE_INT64    = 3
TYPE_UINT64   = 4
TYPE_INT32    = 5
TYPE_FIXED64  = 6
TYPE_FIXED32  = 7
TYPE_BOOL     = 8
TYPE_STRING   = 9
TYPE_MESSAGE  = 11
TYPE_UINT32   = 13
TYPE_ENUM     = 14
TYPE_SINT32   = 17
TYPE_SINT64   = 18

# Proto label
LABEL_OPTIONAL = 1
LABEL_REQUIRED = 2
LABEL_REPEATED = 3


def mode_to_cpp(mode_val):
    """Convert mode enum value to C++ constant."""
    return {
        meta_pb2.MODE_SIMPLE:   "comSimple",
        meta_pb2.MODE_ADVANCED: "comAdvanced",
        meta_pb2.MODE_DEVELOP:  "comDevelop",
    }.get(mode_val, "comAdvanced")


_PRINT_STEPS = None   # set after meta_pb2 import resolves enum values
_OBJECT_STEPS = None


def _init_step_sets():
    global _PRINT_STEPS, _OBJECT_STEPS
    if _PRINT_STEPS is None:
        _PRINT_STEPS = {meta_pb2.STEP_GCODE_EXPORT, meta_pb2.STEP_SKIRT_BRIM, meta_pb2.STEP_WIPE_TOWER}
        _OBJECT_STEPS = {meta_pb2.STEP_SLICE, meta_pb2.STEP_PERIMETERS, meta_pb2.STEP_INFILL, meta_pb2.STEP_SUPPORT}


def step_to_cpp(step_val):
    """Convert invalidation step to C++ constant."""
    return {
        meta_pb2.STEP_GCODE_EXPORT: "psGCodeExport",
        meta_pb2.STEP_SKIRT_BRIM:   "psSkirtBrim",
        meta_pb2.STEP_WIPE_TOWER:   "psWipeTower",
        meta_pb2.STEP_SLICE:        "posSlice",
        meta_pb2.STEP_PERIMETERS:   "posPerimeters",
        meta_pb2.STEP_INFILL:       "posInfill",
        meta_pb2.STEP_SUPPORT:      "posSupportMaterial",
        meta_pb2.STEP_NONE:         "",
    }.get(step_val, "")


def proto_type_to_co_type(field_desc, is_nullable=False):
    """
    Map a protobuf field descriptor to OrcaSlicer's coXXX type constant
    and ConfigOptionXXX class name.

    Returns: (co_type_str, config_option_class, is_vector)
    """
    ftype = field_desc.type
    is_repeated = (field_desc.label == LABEL_REPEATED)
    type_name = field_desc.type_name  # For message types

    # Handle message types (FloatOrPercent, Point2D)
    if ftype == TYPE_MESSAGE:
        if "FloatOrPercent" in type_name:
            if is_repeated:
                return ("coFloatsOrPercents", "ConfigOptionFloatsOrPercents", True)
            return ("coFloatOrPercent", "ConfigOptionFloatOrPercent", False)
        elif "Point2D" in type_name:
            if is_repeated:
                return ("coPoints", "ConfigOptionPoints", True)
            return ("coPoint", "ConfigOptionPoint", False)

    # Handle enum types
    if ftype == TYPE_ENUM:
        if is_repeated:
            return ("coEnums", "ConfigOptionEnumsGeneric", True)
        return ("coEnum", "ConfigOptionEnum", False)

    # Scalar/vector types
    if ftype in (TYPE_FLOAT, TYPE_DOUBLE):
        if is_repeated:
            if is_nullable:
                return ("coFloats", "ConfigOptionFloatsNullable", True)
            return ("coFloats", "ConfigOptionFloats", True)
        return ("coFloat", "ConfigOptionFloat", False)

    if ftype in (TYPE_INT32, TYPE_INT64, TYPE_SINT32, TYPE_SINT64,
                 TYPE_UINT32, TYPE_UINT64, TYPE_FIXED32, TYPE_FIXED64):
        if is_repeated:
            if is_nullable:
                return ("coInts", "ConfigOptionIntsNullable", True)
            return ("coInts", "ConfigOptionInts", True)
        return ("coInt", "ConfigOptionInt", False)

    if ftype == TYPE_BOOL:
        if is_repeated:
            if is_nullable:
                return ("coBools", "ConfigOptionBoolsNullable", True)
            return ("coBools", "ConfigOptionBools", True)
        return ("coBool", "ConfigOptionBool", False)

    if ftype == TYPE_STRING:
        if is_repeated:
            return ("coStrings", "ConfigOptionStrings", True)
        return ("coString", "ConfigOptionString", False)

    return ("coNone", "ConfigOption", False)


def parse_field_options(field_desc_proto):
    """
    Re-parse FieldOptions from a FieldDescriptorProto with extensions registered.
    This is needed because the FileDescriptorSet parser doesn't know about our
    custom extensions, so they end up as unknown fields. Re-parsing with the
    extensions registered (via config_metadata_pb2 import) resolves them.
    """
    from google.protobuf import descriptor_pb2
    opts = field_desc_proto.options
    if not opts.ByteSize():
        return descriptor_pb2.FieldOptions()

    # Re-parse the serialized options with extensions registered
    reparsed = descriptor_pb2.FieldOptions()
    reparsed.ParseFromString(opts.SerializeToString())
    return reparsed


class FieldInfo:
    """Parsed information about a single config field from proto descriptor."""

    def __init__(self, field_desc):
        self.name = field_desc.name
        self.field_desc = field_desc

        # Re-parse options with extensions registered
        opts = parse_field_options(field_desc)

        # Read extensions using the proper protobuf API
        self.label = opts.Extensions[meta_pb2.label] or None
        self.full_label = opts.Extensions[meta_pb2.full_label] or None
        self.tooltip = opts.Extensions[meta_pb2.tooltip] or None
        self.category = opts.Extensions[meta_pb2.category] or None
        self.sidetext = opts.Extensions[meta_pb2.sidetext] or None
        self.min_value = opts.Extensions[meta_pb2.min_value] if opts.HasExtension(meta_pb2.min_value) else None
        self.max_value = opts.Extensions[meta_pb2.max_value] if opts.HasExtension(meta_pb2.max_value) else None
        self.max_literal = opts.Extensions[meta_pb2.max_literal] if opts.HasExtension(meta_pb2.max_literal) else None
        self.mode = opts.Extensions[meta_pb2.mode]  # 0 = MODE_SIMPLE (default)
        self.has_mode = opts.HasExtension(meta_pb2.mode)
        self.ratio_over = opts.Extensions[meta_pb2.ratio_over] or None
        self.multiline = opts.Extensions[meta_pb2.multiline]
        self.full_width = opts.Extensions[meta_pb2.full_width]
        self.height = opts.Extensions[meta_pb2.height] or None
        self.is_nullable = opts.Extensions[meta_pb2.is_nullable]
        self.gui_type = opts.Extensions[meta_pb2.gui_type] or None
        self.gui_flags = opts.Extensions[meta_pb2.gui_flags] or None
        self.enum_keys_map = opts.Extensions[meta_pb2.enum_keys_map_ref] or None
        self.no_cli = opts.Extensions[meta_pb2.no_cli]
        self.readonly = opts.Extensions[meta_pb2.readonly]
        self.preset = opts.Extensions[meta_pb2.preset]  # 0 = PRESET_PRINT
        self.invalidates = list(opts.Extensions[meta_pb2.invalidates])
        self.list_membership = list(opts.Extensions[meta_pb2.list_membership])
        self.legacy_name = opts.Extensions[meta_pb2.legacy_name] or None

        # Default value and enum metadata
        self.has_default = opts.Extensions[meta_pb2.has_default]
        self.default_value = opts.Extensions[meta_pb2.default_value] if self.has_default else None
        self.enum_value_entries = list(opts.Extensions[meta_pb2.enum_value_entries])
        self.enum_label_entries = list(opts.Extensions[meta_pb2.enum_label_entries])
        self.co_type_hint = opts.Extensions[meta_pb2.co_type_hint] or None

        # Resolve C++ type info - co_type_hint overrides auto-detection
        co_type, option_class, is_vec = proto_type_to_co_type(
            field_desc, self.is_nullable)
        if self.co_type_hint:
            co_type = self.co_type_hint
            # Fix up option_class for hint-overridden types
            hint_class_map = {
                "coPercent": "ConfigOptionPercent",
                "coPercents": "ConfigOptionPercents",
                "coEnum": "ConfigOptionEnum",
                "coEnums": "ConfigOptionEnumsGeneric",
            }
            if self.co_type_hint in hint_class_map:
                option_class = hint_class_map[self.co_type_hint]
        self.co_type = co_type
        self.option_class = option_class
        self.is_vector = is_vec


class CodeGenerator:
    """Generates C++ source files from parsed proto descriptors."""

    def __init__(self, descriptor_set):
        self.descriptor_set = descriptor_set
        self.fields = []  # All FieldInfo objects
        self.virtual_keys_by_preset = {  # virtual_preset_keys per preset type
            meta_pb2.PRESET_PRINT: [],
            meta_pb2.PRESET_FILAMENT: [],
            meta_pb2.PRESET_PRINTER: [],
        }
        self._parse_all_fields()

    @staticmethod
    def _preset_type_from_filename(name: str) -> int:
        """Infer preset type from proto filename (printer/filament/print)."""
        n = name.lower()
        if "printer" in n:
            return meta_pb2.PRESET_PRINTER
        if "filament" in n:
            return meta_pb2.PRESET_FILAMENT
        return meta_pb2.PRESET_PRINT

    def _parse_all_fields(self):
        """Parse all message fields from all proto files in the descriptor set."""
        for file_desc in self.descriptor_set.file:
            # Skip google/protobuf imports
            if file_desc.name.startswith("google/"):
                continue
            # Skip config_metadata.proto (it's just extensions, no settings)
            if "config_metadata" in file_desc.name:
                continue

            preset_type = self._preset_type_from_filename(file_desc.name)

            for msg_desc in file_desc.message_type:
                # Skip wrapper messages (FloatOrPercent, Point2D)
                if msg_desc.name in ("FloatOrPercent", "Point2D"):
                    continue

                # Collect message-level virtual_preset_keys
                vkeys = list(msg_desc.options.Extensions[meta_pb2.virtual_preset_keys])
                self.virtual_keys_by_preset[preset_type].extend(vkeys)

                for field_desc in msg_desc.field:
                    self.fields.append(FieldInfo(field_desc))

    def generate_init_fff_params(self) -> str:
        """
        Generate the body of PrintConfigDef::init_fff_params().
        Output: C++ code that's a drop-in replacement for the hand-written registrations.
        """
        lines = []
        lines.append("// ===== AUTO-GENERATED by tools/config_codegen.py =====")
        lines.append("// DO NOT EDIT MANUALLY. Edit .proto files and re-run codegen.")
        lines.append("")

        for field in self.fields:
            lines.append(f'    def = this->add("{field.name}", {field.co_type});')

            if field.label:
                lines.append(f'    def->label = L("{self._escape_cpp(field.label)}");')

            if field.full_label:
                lines.append(f'    def->full_label = L("{self._escape_cpp(field.full_label)}");')

            if field.category:
                lines.append(f'    def->category = L("{self._escape_cpp(field.category)}");')

            if field.tooltip:
                tooltip_escaped = self._escape_cpp(field.tooltip)
                # Split long tooltips across lines
                if len(tooltip_escaped) > 80:
                    lines.append(f'    def->tooltip = L("{tooltip_escaped}");')
                else:
                    lines.append(f'    def->tooltip = L("{tooltip_escaped}");')

            if field.sidetext:
                lines.append(f'    def->sidetext = L("{self._escape_cpp(field.sidetext)}");')

            if field.min_value is not None:
                lines.append(f'    def->min = {self._format_number(field.min_value)};')

            if field.max_value is not None:
                lines.append(f'    def->max = {self._format_number(field.max_value)};')

            if field.max_literal is not None:
                lines.append(f'    def->max_literal = {self._format_number(field.max_literal)};')

            if field.ratio_over:
                lines.append(f'    def->ratio_over = "{field.ratio_over}";')

            if field.has_mode:
                lines.append(f'    def->mode = {mode_to_cpp(field.mode)};')

            if field.is_nullable:
                lines.append(f'    def->nullable = true;')

            if field.readonly:
                lines.append(f'    def->readonly = true;')

            if field.multiline:
                lines.append(f'    def->multiline = true;')

            if field.full_width:
                lines.append(f'    def->full_width = true;')

            if field.height:
                lines.append(f'    def->height = {field.height};')

            if field.gui_type:
                lines.append(f'    def->gui_type = ConfigOptionDef::GUIType::{field.gui_type};')

            if field.gui_flags:
                lines.append(f'    def->gui_flags = "{field.gui_flags}";')

            if field.no_cli:
                lines.append(f'    def->cli = ConfigOptionDef::nocli;')

            if field.enum_keys_map:
                lines.append(f'    def->enum_keys_map = &{field.enum_keys_map};')

            # Enum values/labels
            for ev in field.enum_value_entries:
                lines.append(f'    def->enum_values.push_back("{self._escape_cpp(ev)}");')
            for el in field.enum_label_entries:
                lines.append(f'    def->enum_labels.push_back(L("{self._escape_cpp(el)}"));')

            # Default value - reconstruct full C++ from co_type + default_value
            if field.has_default:
                cpp_expr = self._reconstruct_default_cpp(
                    field.default_value or "", field.co_type, field.enum_keys_map)
                lines.append(f'    def->set_default_value({cpp_expr});')

            lines.append("")

        return "\n".join(lines)

    def generate_preset_options(self) -> str:
        """Generate s_Preset_print_options, s_Preset_filament_options, etc."""
        lines = []
        lines.append("// ===== AUTO-GENERATED by tools/config_codegen.py =====")
        lines.append("")

        for var_name, preset_type in [
            ("s_Preset_print_options",    meta_pb2.PRESET_PRINT),
            ("s_Preset_filament_options", meta_pb2.PRESET_FILAMENT),
            ("s_Preset_printer_options",  meta_pb2.PRESET_PRINTER),
        ]:
            # Field-derived keys + message-level virtual keys, deduplicated and sorted
            field_names = [f.name for f in self.fields if f.preset == preset_type]
            virtual_names = self.virtual_keys_by_preset[preset_type]
            all_names = sorted(set(field_names) | set(virtual_names))

            lines.append(f"static const std::vector<std::string> {var_name} = {{")
            for name in all_names:
                lines.append(f'    "{name}",')
            lines.append("};")
            lines.append("")

        return "\n".join(lines)

    def generate_invalidation_map(self) -> str:
        """Generate opt_key -> invalidation steps mapping, split by PrintStep vs PrintObjectStep."""
        _init_step_sets()
        lines = []
        lines.append("// ===== AUTO-GENERATED by tools/config_codegen.py =====")
        lines.append("")

        lines.append("static const std::unordered_map<std::string, std::vector<PrintStep>> "
                     "s_print_steps_map = {")
        for field in sorted(self.fields, key=lambda x: x.name):
            if field.invalidates:
                steps = [step_to_cpp(s) for s in field.invalidates
                         if s in _PRINT_STEPS and step_to_cpp(s)]
                if steps:
                    lines.append(f'    {{"{field.name}", {{{", ".join(steps)}}}}},')
        lines.append("};")
        lines.append("")

        lines.append("static const std::unordered_map<std::string, std::vector<PrintObjectStep>> "
                     "s_object_steps_map = {")
        for field in sorted(self.fields, key=lambda x: x.name):
            if field.invalidates:
                steps = [step_to_cpp(s) for s in field.invalidates
                         if s in _OBJECT_STEPS and step_to_cpp(s)]
                if steps:
                    lines.append(f'    {{"{field.name}", {{{", ".join(steps)}}}}},')
        lines.append("};")

        return "\n".join(lines)

    def generate_option_key_lists(self) -> str:
        """Generate extruder_option_keys, filament_option_keys, etc."""
        lines = []
        lines.append("// ===== AUTO-GENERATED by tools/config_codegen.py =====")
        lines.append("")

        extruder_keys = [f for f in self.fields
                         if meta_pb2.LIST_EXTRUDER_OPTION_KEYS in f.list_membership]
        filament_keys = [f for f in self.fields
                         if meta_pb2.LIST_FILAMENT_OPTION_KEYS in f.list_membership]

        for var_name, keys in [
            ("s_extruder_option_keys", extruder_keys),
            ("s_filament_option_keys", filament_keys),
        ]:
            lines.append(f"static const std::vector<std::string> {var_name} = {{")
            for f in sorted(keys, key=lambda x: x.name):
                lines.append(f'    "{f.name}",')
            lines.append("};")
            lines.append("")

        return "\n".join(lines)

    @staticmethod
    def _reconstruct_default_cpp(default_value, co_type, enum_keys_map=None):
        """Reconstruct full C++ default expression from co_type + extracted value args.

        Maps (co_type, args) -> 'new ConfigOptionXxx(args)' or 'new ConfigOptionXxx{args}'.
        """
        import re as _re

        # Type -> C++ class mappings
        SCALAR_CLASS = {
            "coFloat":            "ConfigOptionFloat",
            "coBool":             "ConfigOptionBool",
            "coInt":              "ConfigOptionInt",
            "coString":           "ConfigOptionString",
            "coPercent":          "ConfigOptionPercent",
            "coFloatOrPercent":   "ConfigOptionFloatOrPercent",
            "coPoint":            "ConfigOptionPoint",
            "coPoint3":           "ConfigOptionPoint3",
        }
        LIST_CLASS = {
            "coFloats":           "ConfigOptionFloats",
            "coInts":             "ConfigOptionInts",
            "coBools":            "ConfigOptionBools",
            "coStrings":          "ConfigOptionStrings",
            "coPercents":         "ConfigOptionPercents",
            "coFloatsOrPercents": "ConfigOptionFloatsOrPercents",
            "coPoints":           "ConfigOptionPoints",
        }

        # Do NOT unescape \n → newline here; C++ string literals use \n as escape sequence.
        # Only unescape escaped quotes so they appear correctly in the reconstructed expression.
        args = default_value.replace('\\"', '"')

        # Empty args -> default constructor for any type
        if not args:
            if co_type == "coEnum":
                enum_type = "int"
                if enum_keys_map:
                    m = _re.match(r'ConfigOptionEnum<(\w+)>::', enum_keys_map)
                    if m:
                        enum_type = m.group(1)
                return f"new ConfigOptionEnum<{enum_type}>()"
            if co_type == "coEnums":
                return "new ConfigOptionEnumsGeneric{}"
            all_classes = {**SCALAR_CLASS, **LIST_CLASS}
            cls = all_classes.get(co_type, "ConfigOption")
            return f"new {cls}()"

        if co_type in SCALAR_CLASS:
            return f"new {SCALAR_CLASS[co_type]}({args})"

        if co_type in LIST_CLASS:
            return f"new {LIST_CLASS[co_type]}{{{args}}}"

        if co_type == "coEnum":
            # Extract enum type from enum_keys_map, e.g.
            # "ConfigOptionEnum<BedType>::get_enum_values()" -> "BedType"
            enum_type = "int"
            if enum_keys_map:
                m = _re.match(r'ConfigOptionEnum<(\w+)>::', enum_keys_map)
                if m:
                    enum_type = m.group(1)
            return f"new ConfigOptionEnum<{enum_type}>({args})"

        if co_type == "coEnums":
            # List-of-enum: use ConfigOptionEnumsGenericNullable if nullable,
            # otherwise ConfigOptionEnumsGeneric.
            # We don't have is_nullable here, so always emit Generic; caller
            # can override when is_nullable is set.
            return f"new ConfigOptionEnumsGeneric{{ {args} }}"

        # Fallback: try generic
        return f"new ConfigOption({args})"

    @staticmethod
    def _escape_cpp(s):
        """Escape a string for C++ string literal.

        Proto strings already contain C++ escape sequences (\\n, \\", etc.)
        as literal backslash + char. We pass those through and only escape
        unescaped quotes and actual newlines.
        """
        if not s:
            return ""
        # Replace actual newline characters (rare) with \n escape
        s = s.replace('\n', '\\n')
        # Don't double-escape backslashes that are already part of escape sequences.
        # The proto strings store them as literal \n, \", \t etc.
        return s

    @staticmethod
    def _format_number(val):
        """Format a number for C++ (int vs float)."""
        if val is None:
            return "0"
        if isinstance(val, float) and val == int(val):
            return str(int(val))
        return str(val)


def main():
    parser = argparse.ArgumentParser(
        description="Generate C++ config code from protobuf descriptors")
    parser.add_argument("descriptor_set",
                        help="Path to compiled .desc file (protoc --descriptor_set_out)")
    parser.add_argument("output_dir",
                        help="Directory to write generated C++ files")
    args = parser.parse_args()

    # Read descriptor set
    desc_path = Path(args.descriptor_set)
    if not desc_path.exists():
        print(f"ERROR: Descriptor file not found: {desc_path}")
        sys.exit(1)

    with open(desc_path, 'rb') as f:
        raw = f.read()

    file_descriptor_set = descriptor_pb2.FileDescriptorSet()
    file_descriptor_set.ParseFromString(raw)

    print(f"Loaded {len(file_descriptor_set.file)} proto files")
    for fd in file_descriptor_set.file:
        if not fd.name.startswith("google/"):
            print(f"  - {fd.name}: {len(fd.message_type)} messages")

    # Generate code
    gen = CodeGenerator(file_descriptor_set)

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    outputs = {
        "PrintConfigDef_generated.cpp": gen.generate_init_fff_params(),
        "Preset_options_generated.cpp": gen.generate_preset_options(),
        "Invalidation_generated.cpp": gen.generate_invalidation_map(),
        "OptionKeys_generated.cpp": gen.generate_option_key_lists(),
    }

    for filename, content in outputs.items():
        out_path = output_dir / filename
        with open(out_path, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"Generated: {out_path}")

    print(f"\nDone. {len(gen.fields)} settings processed.")


if __name__ == "__main__":
    main()
