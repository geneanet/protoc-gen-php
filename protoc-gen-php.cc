/**
 * PHP Protocol Buffer Generator Plugin for protoc
 * By Andrew Brampton (c) 2010
 *
 * TODO
 *  Support the packed option
 *  Merge multiple messages
 *  Lots of optomisations
 *  Track unknown fields?
 *  Support the deprecated groups encoding
 *  Extensions
 *  Services
 *  Packages
 *  Options
 *  Better validation
 */
#include "strutil.h" // TODO This header is from the offical protobuf source, but it is not normally installed

#include <map>
#include <string>
#include <algorithm>

#include <google/protobuf/descriptor.h>

#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/compiler/code_generator.h>

#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>


using namespace google::protobuf;
using namespace google::protobuf::compiler;

class PHPCodeGenerator : public CodeGenerator {
	private:

		void PrintMessage   (io::Printer &printer, const Descriptor & message) const;
		void PrintMessages  (io::Printer &printer, const FileDescriptor & file) const;

		void PrintEnum      (io::Printer &printer, const EnumDescriptor & e) const;
		void PrintEnums     (io::Printer &printer, const FileDescriptor & file) const;

		void PrintService   (io::Printer &printer, const ServiceDescriptor & service) const;
		void PrintServices  (io::Printer &printer, const FileDescriptor & file) const;

		string DefaultValueAsString(const FieldDescriptor & field, bool quote_string_type) const;

		// Maps names into PHP names
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

string UnderscoresToCamelCaseImpl(const string& input, bool cap_next_letter) {
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
        // Force first letter to lower-case unless explicitly told to
        // capitalize it.
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

string UnderscoresToCamelCase(const FieldDescriptor & field) {
  return UnderscoresToCamelCaseImpl(field.name(), false);
}

string UnderscoresToCapitalizedCamelCase(const FieldDescriptor & field) {
  return UnderscoresToCamelCaseImpl(field.name(), true);
}

string LowerString(const string & s) {
  string newS (s);
  LowerString(&newS);
  return newS;
}

string UpperString(const string & s) {
  string newS (s);
  UpperString(&newS);
  return newS;
}

// Maps a Message full_name into a PHP name
template <class DescriptorType>
string PHPCodeGenerator::ClassName(const DescriptorType & descriptor) const {
	string name (descriptor.full_name());
	replace(name.begin(), name.end(), '.', '_');
	return name;
}

string PHPCodeGenerator::VariableName(const FieldDescriptor & field) const {
	return UnderscoresToCamelCase(field) + '_';
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


void PHPCodeGenerator::PrintMessage(io::Printer &printer, const Descriptor & message) const {

		// Print nested messages
		for (int i = 0; i < message.nested_type_count(); ++i) {
			printer.Print("\n");
			PrintMessage(printer, *message.nested_type(i));
	        }

		// Print nested enum
		for (int i = 0; i < message.enum_type_count(); ++i) {
			PrintEnum(printer, *message.enum_type(i) );
		}

		printer.Print("// `type` `full_name`\n",
		              "type", /* == FieldDescriptor::TYPE_GROUP ? "group" : */ "message",
		              "full_name", message.full_name()
		);

		printer.Print("class `name` {\n",
		              "name", ClassName(message)
		);

		printer.Indent();

		// Print fields map
		vector<const FieldDescriptor *> required_fields;
		printer.Print(
			"// Array maps field indexes to members\n"
			"private static $_map = array (\n"
		);
		printer.Indent();
                for (int i = 0; i < message.field_count(); ++i) {
			const FieldDescriptor &field ( *message.field(i) );

			if (field.is_required())
				required_fields.push_back( &field );

			printer.Print("`index` => '`value`',\n",
				"index", SimpleItoa(field.number()),
				"value", VariableName(field)
			);
		}
		printer.Outdent();
		printer.Print(");\n\n");

		// Constructor
		printer.Print(
			"\n"
			"function __construct($fp = NULL, $limit = PHP_INT_MAX) {\n"
			"  if($fp !== NULL)\n"
			"    $this->read($fp, $limit);\n"
			"}\n"
		);

		// Read
		printer.Print(
			"\n"
			"function read($fp, $limit = PHP_INT_MAX) {\n"
		);
		printer.Indent();

		printer.Print("while(!feof($fp) && $limit > 0) {\n");
		printer.Indent();

		printer.Print(
			"$value = read_varint($fp, &$limit);\n"
			"$wire  = $value & 0x07;\n"
			"$field = $value >> 3;\n"
			"var_dump(\"`name`: Found $field type \" . get_wiretype($wire));\n"
			"switch($field) {\n",
			"name", ClassName(message)
		);
		printer.Indent();
		for (int i = 0; i < message.field_count(); ++i) {
			const FieldDescriptor &field ( *message.field(i) );

			string var ( VariableName(field) );
			if (field.is_repeated())
				var += "[]";
			if (field.is_packable())
				throw "Error we do not yet support packed values";

			string commands;

			switch (field.type()) {
				case FieldDescriptor::TYPE_DOUBLE: // double, exactly eight bytes on the wire
					commands = "ASSERT($wire == 1);\n"
					           "$this->" + var + " = read_double($fp);\n"
					           "$limit-=8;";
					break;

				case FieldDescriptor::TYPE_FLOAT: // float, exactly four bytes on the wire.
					commands = "ASSERT($wire == 5);\n"
					           "$this->" + var + " = read_float($fp);\n"
					           "$limit-=4;";
					break;

				case FieldDescriptor::TYPE_INT64:  // int64, varint on the wire.
				case FieldDescriptor::TYPE_UINT64: // uint64, varint on the wire.
				case FieldDescriptor::TYPE_INT32:  // int32, varint on the wire.
				case FieldDescriptor::TYPE_UINT32: // uint32, varint on the wire
				case FieldDescriptor::TYPE_ENUM:   // Enum, varint on the wire
					commands = "ASSERT($wire == 0);\n"
					           "$this->" + var + " = read_varint($fp, &$limit);";
					break;

				case FieldDescriptor::TYPE_FIXED64: // uint64, exactly eight bytes on the wire.
					commands = "ASSERT($wire == 1);\n"
					           "$this->" + var + " = read_uint64($fp);\n"
					           "$limit-=8;";
					break;

				case FieldDescriptor::TYPE_SFIXED64: // int64, exactly eight bytes on the wire
					commands = "ASSERT($wire == 1);\n"
					           "$this->" + var + " = read_int64($fp);\n"
					           "$limit-=8;";
					break;

				case FieldDescriptor::TYPE_FIXED32: // uint32, exactly four bytes on the wire.
					commands = "ASSERT($wire == 5);\n"
					           "$this->" + var + " = read_uint32($fp);\n"
					           "$limit-=4;";
					break;

				case FieldDescriptor::TYPE_SFIXED32: // int32, exactly four bytes on the wire
					commands = "ASSERT($wire == 5);\n"
					           "$this->" + var + " = read_int32($fp);\n"
					           "$limit-=4;";
					break;

				case FieldDescriptor::TYPE_BOOL: // bool, varint on the wire.
					commands = "ASSERT($wire == 0);\n"
					           "$this->" + var + " = read_varint($fp, &$limit) > 0 ? true : false;";
					break;

				case FieldDescriptor::TYPE_STRING:  // UTF-8 text.
				case FieldDescriptor::TYPE_BYTES:   // Arbitrary byte array.
					commands = "ASSERT($wire == 2);\n"
					           "$len = read_varint($fp, &$limit);\n"
					           "$this->" + var + " = fread($fp, $len);\n"
					           "$limit-=$len;";
					break;

				case FieldDescriptor::TYPE_GROUP: {// Tag-delimited message.  Deprecated.
					const Descriptor & d( *field.message_type() );
					commands = "ASSERT($wire == 3);\n"
					           "$this->" + var + " = new " + ClassName(d) + "($fp, &$limit);\n";
				}
					break;

				case FieldDescriptor::TYPE_MESSAGE: {// Length-delimited message.
					const Descriptor & d( *field.message_type() );
					commands = "ASSERT($wire == 2);\n"
					           "$len = read_varint($fp, &$limit);\n"
					           "$limit-=$len;\n"
					           "$this->" + var + " = new " + ClassName(d) + "($fp, $len);";
				}
					break;

				case FieldDescriptor::TYPE_SINT32:   // int32, ZigZag-encoded varint on the wire
					commands = "ASSERT($wire == 5);\n"
					           "$this->" + var + " = read_zint32($fp);\n"
					           "$limit-=4;";
					break;

				case FieldDescriptor::TYPE_SINT64:   // int64, ZigZag-encoded varint on the wire
					commands = "ASSERT($wire == 1);\n"
					           "$this->" + var + " = read_zint32($fp);\n"
					           "$limit-=8;";
					break;

				default:
					throw "Error: Unsupported type";// TODO use the proper exception
			}

			printer.Print("case `index`:\n", "index", SimpleItoa(field.number()) );

			printer.Indent();

			printer.Print(commands.c_str());
			printer.Print("\nbreak;\n");
			printer.Outdent();
		}

		printer.Print( // TODO Store the unknown field
			"default:\n"
			"  $limit -= skip($fp, $wire);\n"
			"  var_dump(\"`name`: Skipped $field\");",
			"name", ClassName(message)
		);

		printer.Outdent();
		printer.Outdent();
		printer.Print(
			"  }\n"
			"}\n"
		);

		printer.Outdent();
		printer.Print("}\n");

		// Write
		printer.Print(
			"\n"
			"function write($fp, $limit = PHP_INT_MAX) {\n"
		);
		printer.Indent();

		printer.Outdent();
		printer.Print("}\n");


		// Validate that the required fields are included
		printer.Print(
			"\n"
			"public function validateRequired() {\n"
		);
		printer.Indent();
		for (int i = 0; i < required_fields.size(); ++i) {
			printer.Print("if ($this->`name` === null) return false;\n",
				"name", VariableName(*required_fields[i])
			);
		}
		printer.Print("return true;\n");
		printer.Outdent();
		printer.Print("}\n");

		// Print fields variables and methods
                for (int i = 0; i < message.field_count(); ++i) {
                        printer.Print("\n");

			const FieldDescriptor &field ( *message.field(i) );

			map<string, string> variables;
			variables["name"]             = VariableName(field);
			variables["capitalized_name"] = UnderscoresToCapitalizedCamelCase(field);
			variables["default"]          = DefaultValueAsString(field, true);
			variables["comment"]          = field.DebugString();

			if (field.type() == FieldDescriptor::TYPE_GROUP) {
				size_t p = variables["comment"].find ('{');
				if (p != string::npos)
					variables["comment"].resize (p - 1);
			}

			// TODO Check that comment is a single line

			switch (field.type()) {
//				If its a enum we should store it as a int
//				case FieldDescriptor::TYPE_ENUM:
//					variables["type"] = field.enum_type()->name() + " ";
//					break;

				case FieldDescriptor::TYPE_MESSAGE:
				case FieldDescriptor::TYPE_GROUP:
					variables["type"] = ClassName(*field.message_type()) + " ";
					break;

				default:
					variables["type"] = "";
			}

			if (field.is_repeated()) {
				// Repeated field
				printer.Print(variables,
					"// `comment`\n"
					"private $`name` = null;\n"
					"public function clear`capitalized_name`() { $this->`name` = null; }\n"

					"public function get`capitalized_name`Count() { if ($this->`name` === null ) return 0; else return count($this->`name`); }\n"
					"public function get`capitalized_name`($index) { return $this->`name`[$index]; }\n"
					"public function get`capitalized_name`Array() { if ($this->`name` === null ) return array(); else return $this->`name`; }\n"
				);

				// TODO Change the set code to validate input depending on the variable type
				printer.Print(variables,
					"public function set`capitalized_name`($index, $value) {$this->`name`[$index] = $value;	}\n"
					"public function add`capitalized_name`($value) { $this->`name`[] = $value; }\n"
					"public function addAll`capitalized_name`(array $values) { foreach($values as $value) {$this->`name`[] = $value;} }\n"
				);

			} else {
				// Non repeated field
				printer.Print(variables,
					"// `comment`\n"
					"private $`name` = null;\n"
					"public function clear`capitalized_name`() { $this->`name` = null; }\n"
					"public function has`capitalized_name`() { return $this->`name` !== null; }\n"

					"public function get`capitalized_name`() { if($this->`name` === null) return `default`; else return $this->`name`; }\n"
				);

				// TODO Change the set code to validate input depending on the variable type
				printer.Print(variables,
					"public function set`capitalized_name`(`type`$value) { $this->`name` = $value; }\n"
				);
			}
                }

		// Class Insertion Point
		printer.Print(
			"\n"
			"// @@protoc_insertion_point(class_scope:`full_name`)\n",
			"full_name", message.full_name()
		);

		printer.Outdent();
		printer.Print("}\n\n");
}

void PHPCodeGenerator::PrintEnum(io::Printer &printer, const EnumDescriptor & e) const {

		printer.Print("// enum `full_name`\n"
		              "class `name` {\n",
		              "full_name", e.full_name(),
		              "name", ClassName(e)
		);

		printer.Indent();

		// Print fields
                for (int j = 0; j < e.value_count(); ++j) {
			const EnumValueDescriptor &value ( *e.value(j) );

			map<string, string> variables;
			variables["name"]   = UpperString(value.name());
			variables["number"] = SimpleItoa(value.number());

			printer.Print(variables,
				"const `name` = `number`;\n");

                }

		printer.Outdent();
		printer.Print("}\n\n");
}

void PHPCodeGenerator::PrintMessages(io::Printer &printer, const FileDescriptor & file) const {
	for (int i = 0; i < file.message_type_count(); ++i) {
		PrintMessage(printer, *file.message_type(i));
	}
}

void PHPCodeGenerator::PrintEnums(io::Printer &printer, const FileDescriptor & file) const {
	for (int i = 0; i < file.enum_type_count(); ++i) {
		PrintEnum(printer, *file.enum_type(i) );
	}
}

void PHPCodeGenerator::PrintServices(io::Printer &printer, const FileDescriptor & file) const {
	for (int i = 0; i < file.service_count(); ++i) {
		printer.Print("////\n//TODO Service\n////\n");
	}
}

bool PHPCodeGenerator::Generate(const FileDescriptor* file,
				const string& parameter,
				OutputDirectory* output_directory,
				string* error) const {

	string php_filename ( file->name() + ".php" );

	// Generate main file.
	scoped_ptr<io::ZeroCopyOutputStream> output(
		output_directory->Open(php_filename)
	);

	io::Printer printer(output.get(), '`');

	try {
		printer.Print(
			"<?php\n"
			"// Please include the below file before this\n"
			"//require('protocolbuffers.inc.php');\n"
		);

		PrintMessages  (printer, *file);
		PrintEnums     (printer, *file);
		PrintServices  (printer, *file);

		printer.Print("?>");

	} catch (const char *msg) {
		error->assign( msg );
		return false;
	}


	return true;
}

int main(int argc, char* argv[]) {
	PHPCodeGenerator generator;
	return PluginMain(argc, argv, &generator);
}
