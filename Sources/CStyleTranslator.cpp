#include "CStyleTranslator.h"
#include <sstream>

using namespace krafix;

typedef unsigned id;

CStyleTranslator::CStyleTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {

}

const char* CStyleTranslator::indexName(unsigned index) {
	switch (index) {
	case 0:
		return "x";
	case 1:
		return "y";
	case 2:
		return "z";
	case 3:
	default:
		return "w";
	}
}

void CStyleTranslator::indent(std::ofstream& out) {
	for (int i = 0; i < indentation; ++i) {
		out << "\t";
	}
}

void CStyleTranslator::output(std::ofstream& out) {
	outputting = true;
	indent(out);
}

std::string CStyleTranslator::getReference(id _id) {
	if (references.find(_id) == references.end()) {
		std::stringstream str;
		str << "_" << _id;
		return str.str();
	}
	else {
		return references[_id];
	}
}

void CStyleTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;

	switch (inst.opcode) {
	case OpName: {
		Name n;
		unsigned id = inst.operands[0];
		n.name = inst.string;
		names[id] = n;
		break;
	}
	case OpTypePointer: {
		Type t;
		unsigned id = inst.operands[0];
		Type subtype = types[inst.operands[2]];
		t.name = subtype.name;
		types[id] = t;
		break;
	}
	case OpTypeFloat: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "float";
		types[id] = t;
		break;
	}
	case OpTypeInt: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "int";
		types[id] = t;
		break;
	}
	case OpTypeBool: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "bool";
		types[id] = t;
		break;
	}
	case OpConstant: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		std::string value = "unknown";
		if (strcmp(resultType.name, "float") == 0) {
			float f = *(float*)&inst.operands[2];
			std::stringstream strvalue;
			strvalue << f;
			if (strvalue.str().find('.') == std::string::npos) strvalue << ".0";
			value = strvalue.str();
		}
		if (strcmp(resultType.name, "int") == 0) {
			std::stringstream strvalue;
			strvalue << *(int*)&inst.operands[2];
			value = strvalue.str();
		}
		references[result] = value;
		break;
	}
	case OpConstantComposite: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		std::stringstream str;
		str << resultType.name << "(";
		for (unsigned i = 2; i < inst.length; ++i) {
			str << getReference(inst.operands[i]);
			if (i < inst.length - 1) str << ", ";
		}
		str << ")";
		references[result] = str.str();
		break;
	}
	case OpFunctionEnd:
		--indentation;
		output(out);
		out << "}";
		break;
	case OpCompositeExtract: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id composite = inst.operands[2];
		std::stringstream str;
		str << getReference(composite) << "." << indexName(inst.operands[3]);
		references[result] = str.str();
		break;
	}
	case OpVectorShuffle: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id vector1 = inst.operands[2];
		id vector1length = 4; // types[variables[inst.operands[2]].type].length;
		id vector2 = inst.operands[3];
		id vector2length = 4; // types[variables[inst.operands[3]].type].length;
		std::stringstream str;
		str << resultType.name << "(";
		for (unsigned i = 4; i < inst.length; ++i) {
			id index = inst.operands[i];
			if (index < vector1length) str << getReference(vector1) << "." << indexName(index);
			else str << getReference(vector2) << "." << indexName(index - vector1length);
			if (i < inst.length - 1) str << ", ";
		}
		str << ")";
		references[result] = str.str();
		break;
	}
	case OpFMul: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(operand1) << " * " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFAdd: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(operand1) << " + " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpMatrixTimesMatrix: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(operand1) << " * " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpVectorTimesScalar: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id vector = inst.operands[2];
		id scalar = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(vector) << " * " << getReference(scalar) << ")";
		references[result] = str.str();
		break;
	}
	case OpLabel: {
		id label = inst.operands[0];
		if (merges.find(label) != merges.end()) {
			--indentation;
			output(out);
			out << "} // Label " << label;
		}
		else if (labelStarts.find(inst.operands[0]) != labelStarts.end()) {
			output(out);
			out << labelStarts[inst.operands[0]] << "\n";
			indent(out);
			out << "{ // Label " << label;
			++indentation;
		}
		else {
			output(out);
			out << "// Label " << label;
		}
		break;
	}
	case OpBranch:
		output(out);
		out << "// Branch to " << inst.operands[0];
		break;
	case OpSelectionMerge: {
		output(out);
		id label = inst.operands[0];
		unsigned selection = inst.operands[1];
		out << "// Merge " << label << " " << selection;
		merges[label] = 0;
		break;
	}
	case OpBranchConditional: {
		id condition = inst.operands[0];
		id trueLabel = inst.operands[1];
		id falseLabel = inst.operands[2];
		std::stringstream _true;
		_true << "if (" << getReference(condition) << ")";
		labelStarts[trueLabel] = _true.str();
		labelStarts[falseLabel] = "else";
		break;
	}
	case OpDecorate: {
		unsigned target = inst.operands[0];
		Decoration decoration = (Decoration)inst.operands[1];
		if (decoration == DecorationBuiltIn) {
			variables[target].builtin = true;
		}
		break;
	}
	case OpTypeFunction: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "function";
		types[id] = t;
		break;
	}
	case OpIEqual: {
		unsigned result = inst.operands[1];
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " == " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpAccessChain: {
		Type t = types[inst.operands[0]];
		id result = inst.operands[1];
		id base = inst.operands[2];
		id index = inst.operands[3];
		std::stringstream str;
		str << getReference(base) << "[" << getReference(index) << "]";
		references[result] = str.str();
		break;
	}
	case OpLoad: {
		references[inst.operands[1]] = getReference(inst.operands[2]);
		break;
	}
	case OpTypeVoid:
		break;
	case OpEntryPoint:
		break;
	case OpMemoryModel:
		break;
	case OpExtInstImport:
		break;
	case OpSource:
		break;
	default:
		output(out);
		out << "// Unknown operation " << inst.opcode;
		break;
	}
}
