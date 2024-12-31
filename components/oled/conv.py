import re

def convert_icons_to_macros(file_path):
    with open(file_path, 'r') as file:
        content = file.read()

    # Regular expression to match each icon array
    pattern = r'const unsigned char (\w+)\[\] =\s*{([^}]*)};'
    matches = re.findall(pattern, content)

    macros = []
    converted_content = []

    for icon_name, icon_data in matches:
        # Process each line to ensure comments are inline
        lines = icon_data.strip().split('\n')
        formatted_lines = []
        for i, line in enumerate(lines):
            code, *comment = line.split('//', maxsplit=1)  # Split code and comment
            code = code.strip().rstrip(',')  # Keep the code with its original spacing, no trailing comma
            comma = ',' if i < len(lines) - 1 else ''  # Add a comma only if not the last line
            if comment:
                formatted_lines.append(f"{code}{comma} /* {''.join(comment)} */ \\")
            else:
                formatted_lines.append(f"{code}{comma} \\")
        
        # Ensure the final line ends with a backslash
        formatted_lines[-1] = formatted_lines[-1].rstrip('\\') + ' \\'

        initializer = "\n    ".join(formatted_lines)
        macro_name = icon_name.upper()

        # Create macro
        macro = f"#define {macro_name} {{ \\\n    {initializer} \n}}"
        macros.append(macro)

        # Replace array definition with macro usage
        converted_content.append(f"const unsigned char {icon_name}[] = {macro_name};")

    return "\n\n".join(macros), "\n\n".join(converted_content)


# Path to the input file
input_file_path = 'icons.h'  # Replace with your file's name
output_macro_file = 'icons_macros.h'
output_converted_file = 'converted_icons.c'

# Generate macros and converted content
macros, converted_content = convert_icons_to_macros(input_file_path)

# Write macros to a header file
with open(output_macro_file, 'w') as macro_file:
    macro_file.write(macros)

# Write converted content to a C source file
with open(output_converted_file, 'w') as converted_file:
    converted_file.write(converted_content)

print(f"Macros written to {output_macro_file}")
print(f"Converted icons written to {output_converted_file}")
