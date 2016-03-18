#pragma once

#include "Translator.h"
#include <SPIRV/GLSL.std.450.h>
#include <fstream>
#include <sstream>
#include <array>

namespace krafix {

	typedef std::array<std::string, 8> ImageOperandsArray;

	typedef enum {
		kSampledImageUnknown = 0,
		kSampledImageYes = 1,
		kSampledImageNo = 2,
	} SampledImage;

	struct Variable {
		unsigned id;
		unsigned type;
		spv::BuiltIn builtinType;
		spv::StorageClass storage;
		signed location;
		unsigned descriptorSet;
		unsigned binding;
		unsigned offset;
		unsigned stride;
		bool isPerInstance;
		bool builtin;
		bool declared;

		Variable() : id(0), type(0), builtin(false), location(-1), descriptorSet(0),
						binding(0), offset(0), stride(0), isPerInstance(false) {}
	};

	struct Type {
		spv::Op opcode;
		std::string name;
		unsigned baseType;
		unsigned length;
		unsigned byteSize;
		SampledImage sampledImage;
		spv::Dim imageDim;
		bool isDepthImage;
		bool isMultiSampledImage;
		bool isarray;
		bool ispointer;
		std::map<unsigned, std::pair<std::string, Type>> members;

		Type() {
			opcode = spv::OpNop;
			name = "unknown";
			baseType = 0;
			length = 1;
			byteSize = 0;
			isarray = false;
			ispointer = false;
			sampledImage = kSampledImageUnknown;
			imageDim = spv::Dim2D;
			isDepthImage = false;
			isMultiSampledImage = false;
		}
	};

	struct Member {
		unsigned type;
		const char* name;
		spv::BuiltIn builtinType;
		bool builtin;
		bool isColumnMajor;

		Member() : name("unknown"), isColumnMajor(true) {}
	};

	struct Name {
		std::string name;
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

#define ExecutionModeDefault  ( (spv::ExecutionMode) -1 )

	struct ExecutionModes {
		unsigned invocationCount;
		spv::ExecutionMode spacingType;
		spv::ExecutionMode vertexOrder;
		spv::ExecutionMode originOrientation;
		spv::ExecutionMode depthModificationType;
		spv::ExecutionMode primitiveType;
		spv::ExecutionMode outputPrimitiveType;
		unsigned localSize[3];
		unsigned localSizeHint[3];
		unsigned maxVertexCount;
		unsigned vectorTypeHint;
		bool usePixelCenterInteger;
		bool useEarlyFragmentTests;
		bool useTessellationPoints;
		bool useTransformFeedback;
		bool useDepthModification;
		bool disallowContractions;

		ExecutionModes() : localSize{0, 0, 0}, localSizeHint{0, 0, 0} {
			invocationCount = 1;
			spacingType = ExecutionModeDefault;
			vertexOrder = ExecutionModeDefault;
			originOrientation = ExecutionModeDefault;
			depthModificationType = ExecutionModeDefault;
			primitiveType = ExecutionModeDefault;
			outputPrimitiveType = ExecutionModeDefault;
			maxVertexCount = 0;
			vectorTypeHint = 0;
			usePixelCenterInteger = false;
			useEarlyFragmentTests = false;
			useTessellationPoints = false;
			useTransformFeedback = false;
			useDepthModification = false;
			disallowContractions = false;
		}
	};

	class CStyleTranslator : public Translator {
	public:
		CStyleTranslator(std::vector<unsigned>& spirv, EShLanguage stage);
		virtual ~CStyleTranslator();
		virtual void outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst);
		virtual void outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSLstd450 entrypoint);
		void startFunction(std::string name);
		void endFunction();
	protected:
		std::ostream* out;
		std::map<unsigned, Name> names;
		std::map<unsigned, std::string> uniqueNames;
		std::map<unsigned, Type> types;
		std::map<unsigned, Variable> variables;
		std::map<unsigned, Member> members;
		std::map<unsigned, std::string> labelStarts;
		std::map<unsigned, Merge> merges;
		std::map<unsigned, std::string> references;
		std::map<unsigned, std::vector<unsigned>> compositeInserts;
		std::vector<Parameter> parameters;
		std::vector<unsigned> callParameters;
		std::string tempNamePrefix = "kfxT";
		ExecutionModes executionModes;
		int indentation = 0;
		bool outputting = false;
		bool firstFunction = true;
		std::string funcName;
		std::string funcType;
		bool firstLabel = true;
		unsigned entryPoint = -1;
		unsigned vtxIdVarId = -1;
		unsigned instIdVarId = -1;
		unsigned tempNameIndex;
		std::vector<Function*> functions;
		std::ostream* tempout = NULL;
		
		void preprocessInstruction(EShLanguage stage, Instruction& inst);
		virtual std::string indexName(Type& type, const std::vector<std::string>& indices);
		std::string indexName(Type& type, const std::vector<unsigned>& indices);
		void indent(std::ostream* out);
		void output(std::ostream* out);
		std::string getReference(unsigned _id);
		inline unsigned getMemberId(unsigned typeId, unsigned member) { return (typeId << 16) + member; }
		void addUniqueName(unsigned id, const char* name);
		virtual void extractImageOperands(ImageOperandsArray& imageOperands, Instruction& inst, unsigned opIdxStart);
		std::string& getUniqueName(unsigned id, const char* prefix);
		std::string& getVariableName(unsigned id);
		std::string& getFunctionName(unsigned id);
		std::string getNextTempName();
		unsigned getBaseTypeID(unsigned typeID);
		Type& getBaseType(unsigned typeID);
		std::string outputTempVar(std::ostream* out, std::string& tmpTypeName, const std::string& rhs);

		// Preprocessed
		bool isFragDepthUsed = false;
		bool isFragDataUsed = false;
		int fragDataNameId = -1;
		std::vector<unsigned> fragDataIndexIds;
	};
}
