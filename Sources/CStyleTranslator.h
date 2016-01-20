#pragma once

#include "Translator.h"
#include <SPIRV/GLSL.std.450.h>
#include <fstream>
#include <sstream>

namespace krafix {

	typedef enum {
		kSampledImageUnknown = 0,
		kSampledImageYes = 1,
		kSampledImageNo = 2,
	} SampledImage;

	struct Variable {
		unsigned id;
		unsigned type;
		spv::BuiltIn builtinType;
		unsigned location;
		unsigned descriptorSet;
		unsigned binding;
		spv::StorageClass storage;
		bool builtin;
		bool declared;

		Variable() : id(0), type(0), builtin(false), location(0), descriptorSet(0), binding(0) {}
	};

	struct Type {
		spv::Op opcode;
		const char* name;
		unsigned baseType;
		unsigned length;
		SampledImage sampledImage;
		spv::Dim imageDim;
		bool isDepthImage;
		bool isMultiSampledImage;
		bool isarray;

		Type(spv::Op opcode) : opcode(opcode)  {
			name = "unknown";
			baseType = 0;
			length = 1;
			isarray = false;
			sampledImage = kSampledImageUnknown;
			imageDim = spv::Dim2D;
			isDepthImage = false;
			isMultiSampledImage = false;
		}
		Type() : Type(spv::OpNop) {}
	};

	struct Member {
		unsigned type;
		const char* name;
		bool isColumnMajor;

		Member() : name("unknown"), isColumnMajor(true) {}
	};

	struct Name {
		const char* name;
	};

	struct Parameter {
		Type type;
		unsigned id;
	};

	struct Function {
		std::string name;
		std::stringstream text;
	};

	struct Merge {
		bool loop;
	};

	class CStyleTranslator : public Translator {
	public:
		CStyleTranslator(std::vector<unsigned>& spirv, EShLanguage stage);
		virtual ~CStyleTranslator() {}
		virtual void outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst);
		virtual void outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSLstd450 entrypoint);
		void startFunction(std::string name);
		void endFunction();
	protected:
		std::ostream* out;
		std::map<unsigned, Name> names;
		std::map<unsigned, Type> types;
		std::map<unsigned, Variable> variables;
		std::map<unsigned, Member> members;
		std::map<unsigned, std::string> labelStarts;
		std::map<unsigned, Merge> merges;
		std::map<unsigned, std::string> references;
		std::map<unsigned, std::vector<unsigned>> compositeInserts;
		std::vector<Parameter> parameters;
		std::vector<unsigned> callParameters;
		int indentation = 0;
		bool outputting = false;
		bool firstFunction = true;
		std::string funcName;
		std::string funcType;
		bool firstLabel = true;
		unsigned entryPoint = -1;
		std::vector<Function*> functions;
		std::ostream* tempout = NULL;
		
		virtual std::string indexName(const std::vector<unsigned>& indices);
		void indent(std::ostream* out);
		void output(std::ostream* out);
		std::string getReference(unsigned _id);
		inline unsigned getMemberId(unsigned typeId, unsigned member) { return (typeId << 16) + member; }
	};
}
