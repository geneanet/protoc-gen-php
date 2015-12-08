/**
 * PHP Protocol Buffer Generator Plugin for protoc
 * By Andrew Brampton (c) 2010
 *
 * TODO
 *  Support the packed option
 *  Lots of optimisations
 *  Extensions
 *  Services
 *  Packages
 *  Better validation (add code to check setted values are valid)
 *  option optimize_for = CODE_SIZE/SPEED;
 */
#include "strutil.h" // TODO This header is from the offical protobuf source, but it is not normally installed

#include <map>
#include <string>
#include <algorithm>

#include <cstdio> // for sprintf

#include <google/protobuf/descriptor.h>
#include <google/protobuf/wire_format.h>
#include <google/protobuf/wire_format_lite.h>
#include <google/protobuf/wire_format_lite_inl.h>

#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/compiler/code_generator.h>

#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

#include "php_options.pb.h"

using namespace google::protobuf;
using namespace google::protobuf::compiler;
using namespace google::protobuf::internal;

const int STYLE_NB_SPACES = 4;

class PHPCodeGenerator : public CodeGenerator
{
    private:
        void PrintMessage(io::Printer &printer, const Descriptor & message) const;
        void PrintMessages(io::Printer &printer, const FileDescriptor & file) const;

        void PrintEnum(io::Printer &printer, const EnumDescriptor & e, bool is_last) const;
        void PrintEnums(io::Printer &printer, const FileDescriptor & file) const;

        void PrintService(io::Printer &printer, const ServiceDescriptor & service) const;
        void PrintServices(io::Printer &printer, const FileDescriptor & file) const;

        string DefaultValueAsString(const FieldDescriptor & field, bool quote_string_type) const;

        // Print the read() method
        void PrintMessageRead(io::Printer &printer, const Descriptor & message, vector<const FieldDescriptor *> & required_fields, const FieldDescriptor * parentField) const;

        // Print the write() method
        void PrintMessageWrite(io::Printer &printer, const Descriptor & message, const FieldDescriptor * parentField) const;

        // Print the size() method
        void PrintMessageSize(io::Printer &printer, const Descriptor & message) const;

        // Map names into PHP names
        template <class DescriptorType>
        string ClassName(const DescriptorType & descriptor) const;

        string VariableName(const FieldDescriptor & field) const;

    public:
        PHPCodeGenerator();
        ~PHPCodeGenerator();

        bool Generate(const FileDescriptor* file, const string& parameter, OutputDirectory* output_directory, string* error) const;
};

PHPCodeGenerator::PHPCodeGenerator() {}
PHPCodeGenerator::~PHPCodeGenerator() {}

string UnderscoresToCamelCaseImpl(const string& input, bool cap_next_letter)
{
    string result;
    // Note:  I distrust ctype.h due to locales.
    for (int i = 0; i < input.size(); i++) {
        if ('a' <= input[i] && input[i] <= 'z') {
            if (cap_next_letter) {
                result += input[i] + ('A' - 'a');
            } else {
                result += input[i];
            }
            cap_next_letter = false;
        } else if ('A' <= input[i] && input[i] <= 'Z') {
            if (i == 0 && !cap_next_letter) {
                // Force first letter to lower-case unless explicitly told to capitalize it.
                result += input[i] + ('a' - 'A');
            } else {
                // Capital letters after the first are left as-is.
                result += input[i];
            }
            cap_next_letter = false;
        } else if ('0' <= input[i] && input[i] <= '9') {
            result += input[i];
            cap_next_letter = true;
        } else {
            cap_next_letter = true;
        }
    }
    return result;
}

string UnderscoresToCamelCase(const FieldDescriptor & field)
{
    return UnderscoresToCamelCaseImpl(field.name(), false);
}

string UnderscoresToCapitalizedCamelCase(const FieldDescriptor & field)
{
    return UnderscoresToCamelCaseImpl(field.name(), true);
}

string LowerString(const string & s)
{
    string newS (s);
    LowerString(&newS);
    return newS;
}

string UpperString(const string & s)
{
    string newS (s);
    UpperString(&newS);
    return newS;
}

// Map a Message full_name into a PHP name.
template <class DescriptorType>
string PHPCodeGenerator::ClassName(const DescriptorType & descriptor) const
{
    string name (descriptor.name());
    string result;
    remove_copy(name.begin(), name.end(), std::back_inserter(result), '.');
    return result;
}

string PHPCodeGenerator::VariableName(const FieldDescriptor & field) const
{
    return UnderscoresToCamelCase(field);
}

string PHPCodeGenerator::DefaultValueAsString(const FieldDescriptor & field, bool quote_string_type) const {
    switch (field.cpp_type()) {
        case FieldDescriptor::CPPTYPE_INT32:
          return SimpleItoa(field.default_value_int32());

        case FieldDescriptor::CPPTYPE_INT64:
          return SimpleItoa(field.default_value_int64());

        case FieldDescriptor::CPPTYPE_UINT32:
          return SimpleItoa(field.default_value_uint32());

        case FieldDescriptor::CPPTYPE_UINT64:
          return SimpleItoa(field.default_value_uint64());

        case FieldDescriptor::CPPTYPE_FLOAT:
          return SimpleFtoa(field.default_value_float());

        case FieldDescriptor::CPPTYPE_DOUBLE:
          return SimpleDtoa(field.default_value_double());

        case FieldDescriptor::CPPTYPE_BOOL:
          return field.default_value_bool() ? "true" : "false";

        case FieldDescriptor::CPPTYPE_STRING:
          if (quote_string_type)
            return "\"" + CEscape(field.default_value_string()) + "\"";

          if (field.type() == FieldDescriptor::TYPE_BYTES)
            return CEscape(field.default_value_string());

          return field.default_value_string();

        case FieldDescriptor::CPPTYPE_ENUM:
          return ClassName(*field.enum_type()) + "::" + field.default_value_enum()->name();

        case FieldDescriptor::CPPTYPE_MESSAGE:
          return "null";
    }
    return "";
}

void PHPCodeGenerator::PrintMessageRead(io::Printer &printer, const Descriptor & message, vector<const FieldDescriptor *> & required_fields, const FieldDescriptor * parentField) const
{
    map<string, string> vars;

    // Parse the file options.
    const PHPFileOptions & options (message.file()->options().GetExtension(php));
    bool skip_unknown = options.skip_unknown();

    vars["sp"] = string(STYLE_NB_SPACES, ' ');

    // Read.
    printer.Print(
        "\n"
        "public function read($fp, &$limit = PHP_INT_MAX)\n{\n"
    );
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }

    printer.Print("while (!feof($fp) && $limit > 0) {\n");
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }

    vars["name"] = ClassName(message);

    printer.Print(
        vars,
        "$tag = Protobuf::readVarint($fp, $limit);\n"
        "if ($tag === false) {\n"
        "`sp`break;\n"
        "}\n"
        "$wire  = $tag & 0x07;\n"
        "$field = $tag >> 3;\n"
        //"//var_dump(\"`name`: Found $field type \".Protobuf::getWiretype($wire).\" $limit bytes left\");\n"
        "switch ($field) {\n"
    );
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }

    // If we are a group message, we need to add a end group case.
    if (parentField && parentField->type() == FieldDescriptor::TYPE_GROUP) {
        printer.Print("case `index`:\n", "index", SimpleItoa(parentField->number()));
        printer.Print(vars,
            "`sp`assert('$wire == 4');\n"
            "`sp`break 2;\n");
    }

    for (int i = 0; i < message.field_count(); ++i) {
        const FieldDescriptor &field (*message.field(i));

        string var ( VariableName(field) );
        if (field.is_repeated())
            var += "[]";
        if (field.is_packable())
            throw "Error we do not yet support packed values";
        if (field.is_required())
            required_fields.push_back( &field );

        string commands;

        switch (field.type()) {
            case FieldDescriptor::TYPE_DOUBLE: // double, exactly eight bytes on the wire
                commands = "assert('$wire == 1');\n"
                           "$tmp = Protobuf::readDouble($fp);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readDouble returned false');\n"
                           "}\n"
                           "$this->`var` = $tmp;\n"
                           "$limit -= 8;";
                break;

            case FieldDescriptor::TYPE_FLOAT: // float, exactly four bytes on the wire.
                commands = "assert('$wire == 5');\n"
                           "$tmp = Protobuf::readFloat($fp);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readFloat returned false');\n"
                           "}\n"
                           "$this->`var` = $tmp;\n"
                           "$limit -= 4;";
                break;

            case FieldDescriptor::TYPE_INT64:  // int64, varint on the wire.
            case FieldDescriptor::TYPE_UINT64: // uint64, varint on the wire.
            case FieldDescriptor::TYPE_INT32:  // int32, varint on the wire.
            case FieldDescriptor::TYPE_UINT32: // uint32, varint on the wire
            case FieldDescriptor::TYPE_ENUM:   // Enum, varint on the wire
                commands = "assert('$wire == 0');\n"
                           "$tmp = Protobuf::readVarint($fp, $limit);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readVarint returned false');\n"
                           "}\n"
                           "$this->`var` = $tmp;";
                break;

            case FieldDescriptor::TYPE_FIXED64: // uint64, exactly eight bytes on the wire.
                commands = "assert('$wire == 1');\n"
                           "$tmp = Protobuf::readUint64($fp);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readUint64 returned false');\n"
                           "}\n"
                           "$this->`var` = $tmp;\n"
                           "$limit -= 8;";
                break;

            case FieldDescriptor::TYPE_SFIXED64: // int64, exactly eight bytes on the wire
                commands = "assert('$wire == 1');\n"
                           "$tmp = Protobuf::readInt64($fp);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readInt64 returned false');\n"
                           "}\n"
                           "$this->`var` = $tmp;\n"
                           "$limit -= 8;";
                break;

            case FieldDescriptor::TYPE_FIXED32: // uint32, exactly four bytes on the wire.
                commands = "assert('$wire == 5');\n"
                           "$tmp = Protobuf::readUint32($fp);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readUint32 returned false');\n"
                           "}\n"
                           "$this->`var` = $tmp;\n"
                           "$limit -= 4;";
                break;

            case FieldDescriptor::TYPE_SFIXED32: // int32, exactly four bytes on the wire
                commands = "assert('$wire == 5');\n"
                           "$tmp = Protobuf::readInt32($fp);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readInt32 returned false');\n"
                           "}\n"
                           "this->`var` = $tmp\n;"
                           "$limit -= 4;";
                break;

            case FieldDescriptor::TYPE_BOOL: // bool, varint on the wire.
                commands = "assert('$wire == 0');\n"
                           "$tmp = Protobuf::readVarint($fp, $limit);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readVarint returned false');\n"
                           "}\n"
                           "$this->`var` = $tmp > 0 ? true : false;";
                break;

            case FieldDescriptor::TYPE_STRING: // UTF-8 text.
            case FieldDescriptor::TYPE_BYTES: // Arbitrary byte array.
                commands = "assert('$wire == 2');\n"
                           "$len = Protobuf::readVarint($fp, $limit);\n"
                           "if ($len === false) {\n"
                           "`sp`throw new Exception('Protobuf::readVarint returned false');\n"
                           "}\n"
                           "if ($len > 0) {\n"
                           "`sp`$tmp = fread($fp, $len);\n"
                           "} else {\n"
                           "`sp`$tmp = '';\n"
                           "}\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception(\"fread($len) returned false\");\n"
                           "}\n"
                           "$this->`var` = $tmp;\n"
                           "$limit -= $len;";
                break;

            case FieldDescriptor::TYPE_GROUP: { // Tag-delimited message. Deprecated.
                const Descriptor & d(*field.message_type());
                commands = "assert('$wire == 3');\n"
                           "$this->`var` = new " + ClassName(d) + "($fp, $limit);";
                break;
            }

            case FieldDescriptor::TYPE_MESSAGE: { // Length-delimited message.
                const Descriptor & d(*field.message_type());
                commands = "assert('$wire == 2');\n"
                           "$len = Protobuf::readVarint($fp, $limit);\n"
                           "if ($len === false) {\n"
                           "`sp`throw new Exception('Protobuf::readVarint returned false');\n"
                           "}\n"
                           "$limit -= $len;\n"
                           "$this->`var` = new " + ClassName(d) + "($fp, $len);\n"
                           "assert('$len == 0');";
                break;
            }

            case FieldDescriptor::TYPE_SINT32: // int32, ZigZag-encoded varint on the wire
                commands = "assert('$wire == 5');\n"
                           "$tmp = Protobuf::readZint32($fp);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readZint32 returned false');\n"
                           "}\n"
                           "$this->`var` = $tmp;\n"
                           "$limit -= 4;";
                break;

            case FieldDescriptor::TYPE_SINT64: // int64, ZigZag-encoded varint on the wire
                commands = "assert('$wire == 1');\n"
                           "$tmp = Protobuf::readZint64($fp);\n"
                           "if ($tmp === false) {\n"
                           "`sp`throw new Exception('Protobuf::readZint64 returned false');\n"
                           "}\n"
                           "$this->`var` = $tmp;\n"
                           "$limit -= 8;";
                break;

            default:
                throw "Error: Unsupported type";// TODO use the proper exception
        }

        printer.Print("case `index`:\n", "index", SimpleItoa(field.number()) );

        for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
            printer.Indent();
        }
        vars["var"] = var;
        printer.Print(vars, commands.c_str());
        printer.Print("\n\nbreak;\n");
        for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
            printer.Outdent();
        }
    }

    if (skip_unknown) {
        printer.Print(
            vars,
            "default:\n"
            "`sp`$limit -= Protobuf::skipField($fp, $wire);\n"
        );
    } else {
        printer.Print(
            vars,
            "default:\n"
            "`sp`$this->unknown[$field.'-'.Protobuf::getWiretype($wire)][] = Protobuf::readField($fp, $wire, $limit);\n"
        );
    }

    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent(); printer.Outdent();
    }
    printer.Print(
        vars,
        "`sp`}\n" // switch
        "}\n"     // while
    );

    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent();
    }
    printer.Print(
        vars,
        "`sp`if (!$this->validateRequired()) {\n"
        "`sp``sp`throw new Exception('Required fields are missing');\n"
        "`sp`}\n"
        "}\n"
    );
}

/**
 * Turns a 32 bit number into a string suitable for PHP to print out.
 * For example, 0x12345678 would turn into "\x12\x34\x56\78".
 * @param tag
 * @return
 */
string arrayToPHPString(uint8 *a, size_t len)
{
    assert(a != NULL);

    const int dest_length = len * 4 + 1; // Maximum possible expansion
    scoped_array<char> dest(new char[dest_length]);

    char *p = dest.get();

    while (len > 0) {
        uint8 c = *a++;
        if ((c >= 0 && c <= 31) || c >= 127) {
            p += sprintf(p, "\\x%02x", c);
        } else if (c == '"') {
            *p++ = '\\';
            *p++ = c;
        } else {
            *p++ = c;
        }

        len--;
    }

    *p = '\0'; // Null terminate us

    return string(dest.get());
}

/**
 * Some notes
 * Tag    <varint fieldID wireType>
 * Field  <tag> <value>
 * Length <tag> <len> <data>
 * Group  <start tag> <field>+ <end tag>
 * Embedded Message <tag> <len> <field>+
 * Start <field>+ (You have to know what type of Message it is, and it is not length prefixed)
 *
 * The Message class should not print its own length (this should be printed by the parent Message)
 * The Group class should only print its field, the parent should print the start/end tag
 * Otherwise the Message/Group will print everything of the fields.
 */

/**
 * Prints the write() method for this Message
 * @param printer
 * @param message
 * @param parentField
 */
void PHPCodeGenerator::PrintMessageWrite(io::Printer &printer, const Descriptor & message, const FieldDescriptor * parentField) const
{
    map<string, string> vars;

    vars["sp"] = string(STYLE_NB_SPACES, ' ');

    // Write.
    printer.Print(
        "\n"
        "public function write($fp)\n"
        "{\n"
    );
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }

    printer.Print(
        vars,
        "if (!$this->validateRequired()) {\n"
        "`sp`throw new Exception('Required fields are missing');\n"
        "}\n"
    );

    for (int i = 0; i < message.field_count(); ++i) {
        const FieldDescriptor &field ( *message.field(i) );

        if (field.is_packable()) {
            throw "Error we do not yet support packed values";
        }

        // Create the tag.
        uint8 tag[5];
        uint8 *tmp;
        tmp = WireFormatLite::WriteTagToArray(
                field.number(),
                WireFormat::WireTypeForFieldType(field.type()),
                tag);
        int tagLen = tmp - tag;

        string commands;
        switch (field.type()) {
            case FieldDescriptor::TYPE_DOUBLE: // double, exactly eight bytes on the wire
                commands = "Protobuf::writeDouble($fp, `var`);\n";
                break;

            case FieldDescriptor::TYPE_FLOAT: // float, exactly four bytes on the wire.
                commands = "Protobuf::writeFloat($fp, `var`);\n";
                break;

            case FieldDescriptor::TYPE_INT64:  // int64, varint on the wire.
            case FieldDescriptor::TYPE_UINT64: // uint64, varint on the wire.
            case FieldDescriptor::TYPE_INT32:  // int32, varint on the wire.
            case FieldDescriptor::TYPE_UINT32: // uint32, varint on the wire
            case FieldDescriptor::TYPE_ENUM:   // Enum, varint on the wire
                commands = "Protobuf::writeVarint($fp, `var`);\n";
                break;

            case FieldDescriptor::TYPE_FIXED64: // uint64, exactly eight bytes on the wire.
                commands = "Protobuf::writeUint64($fp, `var`);\n";
                break;

            case FieldDescriptor::TYPE_SFIXED64: // int64, exactly eight bytes on the wire
                commands = "Protobuf::writeInt64($fp, `var`);\n";
                break;

            case FieldDescriptor::TYPE_FIXED32: // uint32, exactly four bytes on the wire.
                commands = "Protobuf::writeUint32($fp, `var`);\n";
                break;

            case FieldDescriptor::TYPE_SFIXED32: // int32, exactly four bytes on the wire
                commands = "Protobuf::writeInt32($fp, `var`);\n";
                break;

            case FieldDescriptor::TYPE_BOOL: // bool, varint on the wire.
                commands = "Protobuf::writeVarint($fp, `var` ? 1 : 0);\n";
                break;

            case FieldDescriptor::TYPE_STRING:  // UTF-8 text.
            case FieldDescriptor::TYPE_BYTES:   // Arbitrary byte array.
                commands = "Protobuf::writeVarint($fp, strlen(`var`));\n"
                           "fwrite($fp, `var`);\n";
                break;

            case FieldDescriptor::TYPE_GROUP: {// Tag-delimited message.  Deprecated.
                // The start tag has already been printed, but also print the end tag
                uint8 endtag[5];
                tmp = WireFormatLite::WriteTagToArray(
                        field.number(),
                        WireFormatLite::WIRETYPE_END_GROUP,
                        endtag);
                int endtagLen = tmp - endtag;
                commands = "`var`->write($fp); // group\n"
                           "fwrite($fp, \"" + arrayToPHPString(endtag, endtagLen) + "\");\n";
                break;
            }
            case FieldDescriptor::TYPE_MESSAGE: // Length-delimited message.
                commands = "Protobuf::writeVarint($fp, `var`->size()); // message\n"
                           "`var`->write($fp);\n";
                break;

            case FieldDescriptor::TYPE_SINT32: // int32, ZigZag-encoded varint on the wire
                commands = "Protobuf::writeZint32($fp, `var`);\n";
                break;

            case FieldDescriptor::TYPE_SINT64: // int64, ZigZag-encoded varint on the wire
                commands = "Protobuf::writeZint64($fp, `var`);\n";
                break;

            default:
                throw "Error: Unsupported type"; // TODO use the proper exception
        }

        if (field.is_repeated()) {
            vars["var"] = VariableName(field);
            printer.Print(
                vars,
                "if (!is_null($this->`var`)) {\n"
                "`sp`foreach ($this->`var` as $v) {\n"
            );
            for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
                printer.Indent(); printer.Indent();
            }
            printer.Print("fwrite($fp, \"`tag`\");\n", "tag", arrayToPHPString(tag, tagLen));
            vars["var"] = "$v";
            printer.Print(vars, commands.c_str());
            for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
                printer.Outdent(); printer.Outdent();
            }
            printer.Print(vars, "`sp`}\n}\n");
        } else {
            printer.Print(
                "if (!is_null($this->`var`)) {\n",
                "var", VariableName(field)
            );
            for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
                printer.Indent();
            }
            printer.Print("fwrite($fp, \"`tag`\");\n", "tag", arrayToPHPString(tag, tagLen));
            vars["var"] = "$this->" + VariableName(field);
            printer.Print(vars, commands.c_str());
            for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
                printer.Outdent();
            }
            printer.Print("}\n");
        }
    }

    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent();
    }
    printer.Print("}\n");
}

void PHPCodeGenerator::PrintMessageSize(io::Printer &printer, const Descriptor & message) const
{
    map<string, string> vars;

    vars["sp"] = string(STYLE_NB_SPACES, ' ');

    // Print the calc size method.
    printer.Print(
        vars,
        "\n"
        "public function size()\n"
        "{\n"
        "`sp`$size = 0;\n"
    );
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }

    for (int i = 0; i < message.field_count(); ++i) {
        const FieldDescriptor &field ( *message.field(i) );

        // Calc the size of the tag needed
        int tag = WireFormat::TagSize(field.number(), field.type());

        string command;

        switch (WireFormat::WireTypeForField(&field)) {
            case WireFormatLite::WIRETYPE_VARINT:
                if (field.type() == FieldDescriptor::TYPE_BOOL) {
                    tag++; // A bool will always take 1 byte
                    command = "$size += `tag`;\n";
                } else {
                    command = "$size += `tag` + Protobuf::sizeVarint(`var`);\n";
                }
                break;

            case WireFormatLite::WIRETYPE_FIXED32:
                tag += 4;
                command = "$size += `tag`;\n";
                break;

            case WireFormatLite::WIRETYPE_FIXED64:
                tag += 8;
                command = "$size += `tag`;\n";
                break;

            case WireFormatLite::WIRETYPE_LENGTH_DELIMITED:
                if (field.type() == FieldDescriptor::TYPE_MESSAGE) {
                    command = "$l = `var`->size();\n";
                } else {
                    command = "$l = strlen(`var`);\n";
                }

                command += "$size += `tag` + Protobuf::sizeVarint($l) + $l;\n";
                break;

            case WireFormatLite::WIRETYPE_START_GROUP:
            case WireFormatLite::WIRETYPE_END_GROUP:
                // WireFormat::TagSize returns the tag size * two when using groups, to account for both the start and end tag
                command += "$size += `tag` + `var`->size();\n";
                break;

            default:
                throw "Error: Unsupported wire type";// TODO use the proper exception
        }

        vars["tag"] = SimpleItoa(tag);

        if (field.is_repeated()) {
            vars["var"] = VariableName(field);
            printer.Print(
                vars,
                "if (!is_null($this->`var`)) {\n"
                "`sp`foreach ($this->`var` as $v) {\n"
            );
            for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
                printer.Indent(); printer.Indent();
            }

            vars["var"] = "$v";
            printer.Print(vars, command.c_str());

            for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
                printer.Outdent(); printer.Outdent();
            }
            printer.Print(vars, "`sp`}\n}\n");
        } else {
            printer.Print(
                "if (!is_null($this->`var`)) {\n",
                "var", VariableName(field)
            );
            for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
                printer.Indent();
            }

            vars["var"] = "$this->" + VariableName(field);
            printer.Print(vars, command.c_str());

            for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
                printer.Outdent();
            }
            printer.Print("}\n");
        }
    }
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent();
    }
    printer.Print(
        vars,
        "\n`sp`return $size;\n"
        "}\n"
    );
}

void PHPCodeGenerator::PrintMessage(io::Printer &printer, const Descriptor & message) const
{
    map<string, string> vars;

    // Parse the file options.
    const PHPFileOptions & options (message.file()->options().GetExtension(php));
    bool skip_unknown = options.skip_unknown();

    vars["sp"] = string(STYLE_NB_SPACES, ' ');

    vector<const FieldDescriptor *> required_fields;

    // Print nested messages.
    for (int i = 0; i < message.nested_type_count(); ++i) {
        printer.Print("\n");
        PrintMessage(printer, *message.nested_type(i));
    }

    // Print nested enum.
    for (int i = 0; i < message.enum_type_count(); ++i) {
        PrintEnum(printer, *message.enum_type(i), false);
    }

    // Find out if we are a nested type, if so what kind.
    const FieldDescriptor * parentField = NULL;
    const char * type = "message";
    if (message.containing_type() != NULL) {
        const Descriptor & parent (*message.containing_type());

        // Find which field we are.
        for (int i = 0; i < parent.field_count(); ++i) {
            if (parent.field(i)->message_type() == &message) {
                parentField = parent.field(i);
                break;
            }
        }
        if (parentField->type() == FieldDescriptor::TYPE_GROUP) {
            type = "group";
        }
    }

    // Start printing the message.
    printer.Print("// `type` `full_name`\n",
                  "type", type,
                  "full_name", message.full_name()
    );

    printer.Print("class `name`\n{\n",
                  "name", ClassName(message)
    );
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }

    // Print fields map
    /*
    printer.Print(
        "// Array maps field indexes to members\n"
        "protected static $_map = array (\n"
    );
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }
    for (int i = 0; i < message.field_count(); ++i) {
        const FieldDescriptor &field ( *message.field(i) );

        printer.Print("`index` => '`value`',\n",
            "index", SimpleItoa(field.number()),
            "value", VariableName(field)
        );
    }
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent();
    }
    printer.Print(");\n\n");
    */
    if (!skip_unknown) {
        printer.Print("protected $unknown;\n");
    }

    // Constructor.
    printer.Print(
        vars,
        "\n" // TODO maybe some kind of inheritance would reduce all this code!
        "public function __construct($in = null, &$limit = PHP_INT_MAX)\n"
        "{\n"
        "`sp`if ($in !== null) {\n"
        "`sp``sp`if (is_string($in)) {\n"
        "`sp``sp``sp`$fp = fopen('php://memory', 'r+b');\n"
        "`sp``sp``sp`fwrite($fp, $in);\n"
        "`sp``sp``sp`rewind($fp);\n"
        "`sp``sp`} elseif (is_resource($in)) {\n"
        "`sp``sp``sp`$fp = $in;\n"
        "`sp``sp`} else {\n"
        "`sp``sp``sp`throw new Exception('Invalid in parameter');\n"
        "`sp``sp`}\n"
        "`sp``sp`$this->read($fp, $limit);\n"
        "`sp`}\n"
        "}\n"
    );

    // Print the read/write methods.
    PrintMessageRead(printer, message, required_fields, parentField);
    PrintMessageWrite(printer, message, parentField);

    PrintMessageSize(printer, message);

    // Validate that the required fields are included.
    printer.Print(
        "\n"
        "public function validateRequired()\n"
        "{\n"
    );
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }
    for (int i = 0; i < required_fields.size(); ++i) {
        vars["name"] = VariableName(*required_fields[i]);
        printer.Print(vars,
            "if ($this->`name` === null) {\n"
            "`sp`return false;\n"
            "}\n"
        );
    }
    printer.Print("\nreturn true;\n");
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent();
    }
    printer.Print("}\n");

    // Print a toString method.
    printer.Print(
        vars,
        "\n"
        "public function __toString()\n"
        "{\n"
        "`sp`return ''"
    );
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }

    if (!skip_unknown) {
        printer.Print(vars,
            "\n`sp`.Protobuf::toString('unknown', $this->unknown)");
    }

    for (int i = 0; i < message.field_count(); ++i) {
        const FieldDescriptor &field (*message.field(i));
        vars["name"] = VariableName(field);

        if (field.type() == FieldDescriptor::TYPE_ENUM) {
            vars["enum"] = ClassName(*field.enum_type());
            printer.Print(vars,
                "\n`sp`.Protobuf::toString('`name`', `enum`::toString($this->`name`))"
            );
        } else {
            printer.Print(vars,
                "\n`sp`.Protobuf::toString('`name`', $this->`name`)"
            );
        }
    }
    printer.Print(";\n");
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent();
    }
    printer.Print("}\n");

    // Print fields variables and methods.
    for (int i = 0; i < message.field_count(); ++i) {
        printer.Print("\n");

        const FieldDescriptor &field (*message.field(i));

        vars["name"]             = VariableName(field);
        vars["capitalized_name"] = UnderscoresToCapitalizedCamelCase(field);
        vars["default"]          = DefaultValueAsString(field, true);
        vars["comment"]          = field.DebugString();

        if (field.type() == FieldDescriptor::TYPE_GROUP) {
            size_t p = vars["comment"].find ('{');
            if (p != string::npos) {
                vars["comment"].resize (p - 1);
            }
        }

        // TODO Check that comment is a single line

        switch (field.type()) {
//            If its a enum we should store it as a int
//            case FieldDescriptor::TYPE_ENUM:
//                vars["type"] = field.enum_type()->name() + " ";
//                break;

            case FieldDescriptor::TYPE_MESSAGE:
            case FieldDescriptor::TYPE_GROUP:
                vars["type"] = ClassName(*field.message_type()) + " ";
                break;

            default:
                vars["type"] = "";
        }

        if (field.is_repeated()) {
            // Repeated field.
            printer.Print(vars,
                "// `comment`"
                "`sp`protected $`name` = null;\n"
                "public function clear`capitalized_name`()\n"
                "{\n"
                "`sp`$this->`name` = null;\n"
                "}\n"

                "public function get`capitalized_name`Count()\n"
                "{\n"
                "`sp`if ($this->`name` === null) {\n"
                "`sp``sp`return 0;\n"
                "`sp`} else {\n"
                "`sp``sp`return count($this->`name`);\n"
                "`sp`}\n"
                "}\n"
                "public function get`capitalized_name`($index)\n"
                "{\n"
                "`sp`return $this->`name`[$index];\n"
                "}\n"
                "public function get`capitalized_name`Array()\n"
                "{\n"
                "`sp`if ($this->`name` === null) {\n"
                "`sp``sp`return array();\n"
                "`sp`} else {\n"
                "`sp``sp`return $this->`name`;\n"
                "`sp`}\n"
                "}\n"
            );

            // TODO Change the set code to validate input depending on the variable type.
            printer.Print(vars,
                "public function set`capitalized_name`($index, $value)\n"
                "{\n"
                "`sp`$this->`name`[$index] = $value;\n"
                "}\n"
                "public function add`capitalized_name`($value)\n"
                "{\n"
                "`sp`$this->`name`[] = $value;\n"
                "}\n"
                "public function addAll`capitalized_name`(array $values)\n"
                "{\n"
                "`sp`foreach ($values as $value) {\n"
                "`sp``sp`$this->`name`[] = $value;\n"
                "`sp`}\n"
                "}\n"
            );
        } else {
            // Non repeated field.
            printer.Print(vars,
                "// `comment`"
                "`sp`protected $`name` = null;\n"
                "public function clear`capitalized_name`()\n"
                "{\n"
                "`sp`$this->`name` = null;\n"
                "}\n"
                "public function has`capitalized_name`()\n"
                "{\n"
                "`sp`return $this->`name` !== null;\n"
                "}\n"

                "public function get`capitalized_name`()\n"
                "{\n"
                "`sp`if ($this->`name` === null) {\n"
                "`sp``sp`return `default`;\n"
                "`sp`} else {\n"
                "`sp``sp`return $this->`name`;\n"
                "`sp`}\n"
                "}\n"
            );

            // TODO Change the set code to validate input depending on the variable type.
            printer.Print(vars,
                "public function set`capitalized_name`(`type`$value)\n"
                "{\n"
                "`sp`$this->`name` = $value;\n"
                "}\n"
            );
        }
    }

    // Class Insertion Point
    printer.Print(
        "\n"
        "// @@protoc_insertion_point(class_scope:`full_name`)\n",
        "full_name", message.full_name()
    );

    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent();
    }
    printer.Print("}\n\n");
}

void PHPCodeGenerator::PrintEnum(io::Printer &printer, const EnumDescriptor & e, bool is_last) const
{
    map<string, string> vars;

    vars["sp"] = string(STYLE_NB_SPACES, ' ');

    printer.Print("// enum `full_name`\n"
                  "class `name`\n{\n",
                  "full_name", e.full_name(),
                  "name", ClassName(e)
    );

    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }

    // Print fields.
    for (int j = 0; j < e.value_count(); ++j) {
        const EnumValueDescriptor &value ( *e.value(j) );

        printer.Print(
            "const `name` = `number`;\n",
            "name", UpperString(value.name()),
            "number", SimpleItoa(value.number())
        );
    }

    // Print values array.
    printer.Print("\npublic static $values = array(\n");
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Indent();
    }
    for (int j = 0; j < e.value_count(); ++j) {
        const EnumValueDescriptor &value ( *e.value(j) );

        printer.Print(
            "`number` => self::`name`,\n",
            "number", SimpleItoa(value.number()),
            "name", UpperString(value.name())
        );
    }
    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent();
    }
    printer.Print(");\n\n");

    // Print a toString.
    printer.Print(
        vars,
        "public static function toString($value)\n"
        "{\n"
        "`sp`if (is_null($value)) {\n"
        "`sp``sp`return null;\n"
        "`sp`}\n"
        "`sp`if (array_key_exists($value, self::$values)) {\n"
        "`sp``sp`return self::$values[$value];\n"
        "`sp`}\n"
        "\n"
        "`sp`return 'UNKNOWN';\n"
        "}\n"
    );

    for (int i = 0; i < STYLE_NB_SPACES / 2; ++i) {
        printer.Outdent();
    }
    printer.Print("}\n");
    if (!is_last) {
        printer.Print("\n");
    }
}

void PHPCodeGenerator::PrintMessages(io::Printer &printer, const FileDescriptor & file) const
{
    for (int i = 0; i < file.message_type_count(); ++i) {
        PrintMessage(printer, *file.message_type(i));
    }
}

void PHPCodeGenerator::PrintEnums(io::Printer &printer, const FileDescriptor & file) const
{
    bool is_last = false;
    for (int i = 0; i < file.enum_type_count(); ++i) {
        if (i == file.enum_type_count() - 1) {
            is_last = true;
        }
        PrintEnum(printer, *file.enum_type(i), is_last);
    }
}

void PHPCodeGenerator::PrintServices(io::Printer &printer, const FileDescriptor & file) const
{
    for (int i = 0; i < file.service_count(); ++i) {
        printer.Print("////\n//TODO Service\n////\n");
    }
}

bool PHPCodeGenerator::Generate(const FileDescriptor* file,
                const string& parameter,
                OutputDirectory* output_directory,
                string* error) const
{
    string php_filename (file->name() + ".php");

    // Parse the options.
    const PHPFileOptions & options (file->options().GetExtension(php));
    const string & namespace_ (options.namespace_());

    // Generate main file.
    scoped_ptr<io::ZeroCopyOutputStream> output(
        output_directory->Open(php_filename)
    );

    io::Printer printer(output.get(), '`');

    try {
        printer.Print(
            "<?php\n"
            "// Please include the below file before `filename`\n"
            "//require('protocolbuffers.inc.php');\n",
            "filename", php_filename.c_str()
        );

        if (!namespace_.empty()) {
            printer.Print("namespace `ns`;\n\n", "ns", namespace_.c_str());
        }

        printer.Print(
            "use Exception;\n"
            "use Protobuf;\n\n"
        );

        PrintMessages (printer, *file);
        PrintEnums    (printer, *file);
        PrintServices (printer, *file);

    } catch (const char *msg) {
        error->assign( msg );
        return false;
    }

    return true;
}

int main(int argc, char* argv[])
{
    PHPCodeGenerator generator;
    return PluginMain(argc, argv, &generator);
}
