#include "CStyleTranslator.h"
#include <stdio.h>
#include <string.h>

using namespace krafix;

typedef unsigned id;

CStyleTranslator::CStyleTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {

}

std::string CStyleTranslator::indexName(Type& type, const std::vector<unsigned>& indices) {
	std::stringstream str;
	for (unsigned i = 0; i < indices.size(); ++i) {
		if (type.members.find(indices[i]) != type.members.end()) {
			if (strncmp(type.name, "gl_", 3) != 0) str << ".";
			str << std::get<0>(type.members[indices[i]]);
		}
		else {
			str << "[";
			str << indices[i];
			str << "]";
		}
	}
	return str.str();
}

void CStyleTranslator::indent(std::ostream* out) {
	for (int i = 0; i < indentation; ++i) {
		(*out) << "\t";
	}
}

void CStyleTranslator::output(std::ostream* out) {
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

void CStyleTranslator::startFunction(std::string name) {
	tempout = out;
	Function* func = new Function;
	func->name = name;
	functions.push_back(func);
	out = &func->text;
}

void CStyleTranslator::endFunction() {
	out = tempout;
}

void CStyleTranslator::outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSLstd450 entrypoint) {
	id result = inst.operands[1];
	switch (entrypoint) {
	case GLSLstd450FAbs: {
		std::stringstream str;
		str << "abs(" << getReference(inst.operands[4]) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Normalize: {
		std::stringstream str;
		str << "normalize(" << getReference(inst.operands[4]) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FClamp: {
		id x = inst.operands[4];
		id minVal = inst.operands[5];
		id maxVal = inst.operands[6];
		std::stringstream str;
		str << "clamp(" << getReference(x) << ", " << getReference(minVal) << ", " << getReference(maxVal) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Pow: {
		id x = inst.operands[4];
		id y = inst.operands[5];
		std::stringstream str;
		str << "pow(" << getReference(x) << ", " << getReference(y) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450InverseSqrt: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "inversesqrt(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FMin: {
		std::stringstream str;
		str << "min(" << getReference(inst.operands[4]) << ", " << getReference(inst.operands[5]) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Cross: {
		id x = inst.operands[4];
		id y = inst.operands[5];
		std::stringstream str;
		str << "cross(" << getReference(x) << ", " << getReference(y) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Sin: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "sin(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Cos: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "cos(" << getReference(x)  << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Asin: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "asin(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Sqrt: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "sqrt(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Length: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "length(" << getReference(x) << ")"; //TODO
		references[result] = str.str();
		break;
	}
	case GLSLstd450Exp2: {
		std::stringstream str;
		str << "exp2(" << getReference(inst.operands[4]) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Distance: {
		std::stringstream str;
		id p0 = inst.operands[4];
		id p1 = inst.operands[5];
		str << "distance(" << getReference(p0) << ", " << getReference(p1) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FMix: {
		std::stringstream str;
		id x = inst.operands[4];
		id y = inst.operands[5];
		id a = inst.operands[6];
		str << "mix(" << getReference(x) << ", " << getReference(y) << ", " << getReference(a) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Floor: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "floor(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Exp: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "exp(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Log2: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "log2(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FSign: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "sign(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Ceil: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "ceil(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Fract: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "fract(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FMax: {
		std::stringstream str;
		id p0 = inst.operands[4];
		id p1 = inst.operands[5];
		str << "max(" << getReference(p0) << ", " << getReference(p1) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Step: {
		std::stringstream str;
		id edge = inst.operands[4];
		id x = inst.operands[5];
		str << "step(" << getReference(edge) << ", " << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450SmoothStep: {
		std::stringstream str;
		id edge0 = inst.operands[4];
		id edge1 = inst.operands[5];
		id x = inst.operands[6];
		str << "smoothstep(" << getReference(edge0) << ", " << getReference(edge1) << ", " << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Reflect: {
		std::stringstream str;
		id I = inst.operands[4];
		id N = inst.operands[5];
		str << "reflect(" << getReference(I) << ", " << getReference(N) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Acos: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "acos(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Atan: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "atan(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450MatrixInverse: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "inverse(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Determinant: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "determinant(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	default:
		output(out);
		(*out) << "// Unknown GLSL instruction " << entrypoint;
		break;
	}
}

void CStyleTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;

	switch (inst.opcode) {
	case OpName: {
		unsigned id = inst.operands[0];
		if (strcmp(inst.string, "") != 0) {
			Name n; 
			n.name = inst.string;
			names[id] = n;
		}
		break;
	}
	case OpTypePointer: {
		Type t;
		unsigned id = inst.operands[0];
		Type& subtype = types[inst.operands[2]];
		t.name = subtype.name;
		t.isarray = subtype.isarray;
		t.length = subtype.length;
		t.members = subtype.members;
		t.ispointer = true;
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
	case OpTypeStruct: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		Name n = names[id];
		t.name = n.name;
		for (unsigned i = 1; i < inst.length; ++i) {
			Type& membertype = types[inst.operands[i]];
			std::get<1>(t.members[i - 1]) = membertype;
		}
		break;
	}
	case OpConstant: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
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
	case OpConstantTrue: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		references[result] = "true";
		break;
	}
	case OpConstantFalse: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		references[result] = "false";
		break;
	}
	case OpConstantComposite: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
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
		(*out) << "}";
		endFunction();
		break;
	case OpExecutionMode: {
		output(out);
		switch (inst.operands[1]) {
		case ExecutionModeInvocations:

			break;
		case ExecutionModeTriangles:
			(*out) << "layout(triangles) in;";
			break;
		case ExecutionModeOutputTriangleStrip:
			if (stage == EShLangGeometry) {
				(*out) << "layout(triangle_strip, max_vertices = 3) out;";
			}
			break;
		case ExecutionModeSpacingEqual:
			if (stage == EShLangTessEvaluation) {
				(*out) << "layout(equal_spacing) in;";
			}
			break;
		case ExecutionModeVertexOrderCw:
			if (stage == EShLangTessEvaluation) {
				(*out) << "layout(cw) in;";
			}
			break;
		case ExecutionModeOutputVertices:
			if (stage == EShLangTessControl) {
				(*out) << "layout(vertices = " << inst.operands[2] << ") out;";
			}
			break;
		default:
			(*out) << "// Unknown execution mode " << inst.operands[1];
		}
		break;
	}
	case OpTypeArray: {
		Type t;
		t.name = "unknownarray";
		t.isarray = true;
		unsigned id = inst.operands[0];
		Type subtype = types[inst.operands[1]];
		t.length = atoi(references[inst.operands[2]].c_str());
		if (subtype.name != NULL) {
			if (strcmp(subtype.name, "float") == 0) {
				t.name = "float";
			}
			else if (strcmp(subtype.name, "vec2") == 0) {
				t.name = "vec2";
			}
			else if (strcmp(subtype.name, "vec3") == 0) {
				t.name = "vec3";
			}
			else if (strcmp(subtype.name, "vec4") == 0) {
				t.name = "vec4";
			}
		}
		types[id] = t;
		break;
	}
	case OpTypeVector: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "vec?";
		Type subtype = types[inst.operands[1]];
		if (subtype.name != NULL) {
			if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 2) {
				t.name = "vec2";
				t.length = 2;
			}
			else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 3) {
				t.name = "vec3";
				t.length = 3;
			}
			else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 4) {
				t.name = "vec4";
				t.length = 4;
			}
		}
		types[id] = t;
		break;
	}
	case OpTypeMatrix: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "mat?";
		Type subtype = types[inst.operands[1]];
		if (subtype.name != NULL) {
			if (strcmp(subtype.name, "vec2") == 0 && inst.operands[2] == 2) {
				t.name = "mat2";
				t.length = 4;
				types[id] = t;
			}
			if (strcmp(subtype.name, "vec3") == 0 && inst.operands[2] == 3) {
				t.name = "mat3";
				t.length = 4;
				types[id] = t;
			}
			else if (strcmp(subtype.name, "vec4") == 0 && inst.operands[2] == 4) {
				t.name = "mat4";
				t.length = 4;
				types[id] = t;
			}
		}
		break;
	}
	case OpTypeImage: {
		Type t;
		unsigned id = inst.operands[0];
		bool video = inst.length >= 8 && inst.operands[8] == 1;
		if (video && target.system == Android) {
			t.name = "samplerExternalOES";
		}
		else {
			t.name = "sampler2D";
		}
		types[id] = t;
		break;
	}
	case OpTypeSampler: {
		break;
	}
	case OpTypeSampledImage: {
		Type t;
		unsigned id = inst.operands[0];
		unsigned image = inst.operands[1];
		types[id] = types[image];
		break;
	}
	case OpMemberName: {
		Type& type = types[inst.operands[0]];
		id number = inst.operands[1];
		std::get<0>(type.members[number]) = (char*)&inst.operands[2];
		break;
	}
	case OpMemberDecorate: {
		break;
	}
	case OpVariable: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		Variable& v = variables[result];
		v.id = result;
		v.type = inst.operands[0];
		v.storage = (StorageClass)inst.operands[2];
		v.declared = true; //v.storage == StorageClassInput || v.storage == StorageClassOutput || v.storage == StorageClassUniformConstant;
		if (names.find(result) != names.end()) {
			if (target.version >= 300 && v.storage == StorageClassOutput && stage == EShLangFragment) {
				names[result].name = "krafix_FragColor";
			}
			std::stringstream name;
			name << names[result].name;
			if (v.storage == StorageClassFunction && getReference(result) != "param") name << "_" << result;
			references[result] = name.str();
		}
		if (v.storage == StorageClassFunction && getReference(result) != "param") {
			output(out);
			Type t = types[v.type];
			if (t.isarray) {
				(*out) << t.name << " " << getReference(result) << "[" << t.length << "];";
			}
			else {
				(*out) << t.name << " " << getReference(result) << ";";
			}
		}
		break;
	}
	case OpPhi: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		output(out);
		(*out) << resultType.name << " " << getReference(result) << ";\n";

		bool first = true;
		
		for (unsigned i = 2; i < inst.length; i += 2) {
			id variable = inst.operands[i];
			id parent = inst.operands[i + 1];
		
			if (labelStarts.find(parent) != labelStarts.end()) {
				indent(out);
				if (!first) (*out) << "else ";
				(*out) << labelStarts[parent] << "\n";
				++indentation;
				indent(out);
				(*out) << getReference(result) << " = " << getReference(variable) << ";\n";
				--indentation;

				first = false;
			}
		}

		for (unsigned i = 2; i < inst.length; i += 2) {
			id variable = inst.operands[i];
			id parent = inst.operands[i + 1];
			
			if (labelStarts.find(parent) == labelStarts.end()) {
				indent(out);
				(*out) << "else\n";
				++indentation;
				indent(out);
				(*out) << getReference(result) << " = " << getReference(variable) << ";\n";
				--indentation;
			}
		}

		references[result] = getReference(result);
		break;
	}
	case OpCompositeExtract: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id composite = inst.operands[2];
		std::stringstream str;
		std::vector<unsigned> indices;
		for (unsigned i = 3; i < inst.length; ++i) {
			indices.push_back(inst.operands[i]);
		}
		str << getReference(composite) << indexName(types[composite], indices);
		references[result] = str.str();
		break;
	}
	case OpVectorShuffle: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id vector1 = inst.operands[2];
		id vector1length = types[vector1].length;
		id vector2 = inst.operands[3];
		id vector2length = types[vector2].length;

		std::stringstream str;
		str << resultType.name << "(";
		for (unsigned i = 4; i < inst.length; ++i) {
			id index = inst.operands[i];
			std::vector<unsigned> indices1;
			indices1.push_back(index);
			std::vector<unsigned> indices2;
			indices2.push_back(index - vector1length);
			if (index < vector1length) str << getReference(vector1) << indexName(types[vector1], indices1);
			else str << getReference(vector2) << indexName(types[vector2], indices2);
			if (i < inst.length - 1) str << ", ";
		}
		str << ")";
		references[result] = str.str();
		break;
	}
	case OpFMul: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(operand1) << " * " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpIMul: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
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
		types[result] = resultType;
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
		types[result] = resultType;
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(operand1) << " * " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpMatrixTimesScalar: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id matrix = inst.operands[2];
		id scalar = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(matrix) << " * " << getReference(scalar) << ")";
		references[result] = str.str();
		break;
	}
	case OpVectorTimesScalar: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id vector = inst.operands[2];
		id scalar = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(vector) << " * " << getReference(scalar) << ")";
		references[result] = str.str();
		break;
	}
	case OpFOrdGreaterThan: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " > " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFOrdLessThanEqual: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " <= " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFOrdNotEqual: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " != " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpLogicalAnd: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " && " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFSub: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " - " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpDot: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "dot(" << getReference(op1) << ", " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFDiv: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " / " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpVectorTimesMatrix: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id vector = inst.operands[2];
		id matrix = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(vector) << " * " << getReference(matrix) << ")";
		references[result] = str.str();
		break;
	}
	case OpConvertSToF: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id value = inst.operands[2];
		std::stringstream str;
		str << "float(" << getReference(value) << ")";
		references[result] = str.str();
		break;
	}
	case OpTranspose: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id matrix = inst.operands[2];
		std::stringstream str;
		str << "transpose(" << getReference(matrix) << ")";
		references[result] = str.str();
		break;
	}
	case OpSelect: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id condition = inst.operands[2];
		id obj1 = inst.operands[3];
		id obj2 = inst.operands[4];
		std::stringstream str;
		str << "(" << getReference(condition) << " ? " << getReference(obj1) << " : " << getReference(obj2) << ")";
		references[result] = str.str();
		break;
	}
	case OpCompositeInsert: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id object = inst.operands[2];
		id composite = inst.operands[3];
		std::stringstream str;
		str << getReference(object);
		references[result] = str.str();
		std::vector<unsigned> indices;
		for (unsigned i = 4; i < inst.length; ++i) {
			indices.push_back(inst.operands[i]);
		}
		compositeInserts[result] = indices;
		break;
	}
	case OpFunctionCall: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id func = inst.operands[2];
		std::string funcname = names[func].name;
		funcname = funcname.substr(0, funcname.find_first_of('('));
		std::stringstream str;
		str << funcname << "(";
		for (unsigned i = 3; i < inst.length; ++i) {
			str << getReference(inst.operands[i]);
			if (i < inst.length - 1) str << ", ";
		}
		str << ")";
		references[result] = str.str();
		break;
	}
	case OpExtInst: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id set = inst.operands[2];
		{
			GLSLstd450 instruction = (GLSLstd450)inst.operands[3];
			outputLibraryInstruction(target, attributes, inst, instruction);
		}
		break;
	}
	case OpFunctionParameter: {
		Parameter param;
		param.type = types[inst.operands[0]];
		param.id = inst.operands[1];
		parameters.push_back(param);
		if (names.find(param.id) != names.end()) {
			references[param.id] = names[param.id].name;
		}
		break;
	}
	case OpLabel: {
		id label = inst.operands[0];
		if (labelStarts.find(inst.operands[0]) != labelStarts.end()) {
			if (labelStarts[inst.operands[0]].at(0) == '}') {
				--indentation;
			}
			output(out);
			(*out) << labelStarts[inst.operands[0]] << "\n";
			indent(out);
			(*out) << "{ // Label " << label;
			++indentation;
		}
		else if (merges.find(label) != merges.end()) {
			--indentation;
			output(out);
			(*out) << "} // Label " << label;
		}
		else {
			output(out);
			(*out) << "// Label " << label;
		}
		break;
	}
	case OpBranch: {
		id branch = inst.operands[0];
		output(out);
		(*out) << "// Branch to " << branch;
		break;
	}
	case OpSelectionMerge: {
		output(out);
		id label = inst.operands[0];
		unsigned selection = inst.operands[1];
		(*out) << "// Merge " << label << " " << selection;
		Merge merge;
		merge.loop = false;
		merges[label] = merge;
		break;
	}
	case OpLoopMerge: {
		output(out);
		id label = inst.operands[0];
		unsigned selection = inst.operands[1];
		(*out) << "// Merge " << label << " " << selection;
		Merge merge;
		merge.loop = true;
		merges[label] = merge;
		break;
	}
	case OpBranchConditional: {
		id condition = inst.operands[0];
		id trueLabel = inst.operands[1];
		id falseLabel = inst.operands[2];

		bool foundMerge = false;
		bool loop = false;
		if (merges.find(falseLabel) != merges.end()) {
			foundMerge = true;
			loop = merges[falseLabel].loop;
		}

		std::stringstream _true;
		if (loop) _true << "while (" << getReference(condition) << ") // true: " << trueLabel << " false: " << falseLabel;
		else _true << "if (" << getReference(condition) << ") // true: " << trueLabel << " false: " << falseLabel;
		labelStarts[trueLabel] = _true.str();
		if (!foundMerge) {
			std::stringstream str;
			str << "}\n";
			for (int i = 0; i < indentation; ++i) str << "\t";
			str << "else";
			labelStarts[falseLabel] = str.str();
		}
		break;
	}
	case OpReturnValue: {
		output(out);
		(*out) << "return " << getReference(inst.operands[0]) << ";";
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
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " == " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpIAdd: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " + " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpFOrdLessThan: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " < " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpSLessThan: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " < " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpSLessThanEqual: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " <= " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpFNegate: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand = inst.operands[2];
		std::stringstream str;
		str << "-" << getReference(operand);
		references[result] = str.str();
		break;
	}
	case OpAccessChain: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id base = inst.operands[2];
		std::stringstream str;
		if (strncmp(types[base].name, "gl_", 3) != 0) str << getReference(base);
		for (unsigned i = 3; i < inst.length; ++i) {
			std::vector<unsigned> indices;
			indices.push_back(atoi(getReference(inst.operands[i]).c_str()));
			str << indexName(types[base], indices);
		}
		references[result] = str.str();
		break;
	}
	case OpLoad: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		references[result] = getReference(inst.operands[2]);
		break;
	}
	case OpFOrdEqual: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " == " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpFOrdGreaterThanEqual: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " >= " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpFMod: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << "mod(" << getReference(operand1) << ", " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpISub: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " - " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpLogicalOr: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " || " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpConvertFToS: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned value = inst.operands[2];
		std::stringstream str;
		str << "int(" << getReference(value) << ")";
		references[result] = str.str();
		break;
	}
	case OpLogicalNot: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned value = inst.operands[2];
		std::stringstream str;
		str << "!" << getReference(value);
		references[result] = str.str();
		break;
		break;
	}
	case OpEmitVertex:
		output(out);
		(*out) << "EmitVertex();";
		break;
	case OpEndPrimitive:
		output(out);
		(*out) << "EndPrimitive();";
		break;
	case OpFunction: {
		firstLabel = true;
		parameters.clear();
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		if (result == entryPoint) {
			references[result] = "main";
			funcName = "main";
			funcType = "void";
		}
		else if (names.find(result) != names.end()) {
			std::string name = names[result].name;
			name = name.substr(0, name.find_first_of('('));
			references[result] = name;
			funcName = name;
			funcType = resultType.name;
		}
		break;
	}
	case OpCompositeConstruct: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
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
	case OpMatrixTimesVector: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id matrix = inst.operands[2];
		id vector = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(matrix) << " * " << getReference(vector) << ")";
		references[result] = str.str();
		break;
	}
	case OpImageSampleImplicitLod: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id sampler = inst.operands[2];
		id coordinate = inst.operands[3];
		std::stringstream str;
		if (target.version < 300) str << "texture2D";
		else str << "texture";
		str << "(" << getReference(sampler) << ", " << getReference(coordinate) << ")";
		references[result] = str.str();
		break;
	}
	case OpUndef: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		if (strcmp(resultType.name, "bool") == 0) {
			references[result] = "false";
		}
		else {
			references[result] = "0";
		}
		break;
	}
	case OpReturn:
		output(out);
		(*out) << "return;";
		break;
	case OpKill:
		output(out);
		(*out) << "discard;";
		break;
	case OpStore: {
		Variable& v = variables[inst.operands[0]];
		if (getReference(inst.operands[0]) == "param") {
			references[inst.operands[0]] = getReference(inst.operands[1]);
		}
		else if (stage == EShLangFragment && v.storage == StorageClassOutput && target.version < 300) {
			output(out);
			if (compositeInserts.find(inst.operands[1]) != compositeInserts.end()) {
				Type type;
				(*out) << "gl_FragColor" << indexName(type, compositeInserts[inst.operands[1]]) << " = " << getReference(inst.operands[1]) << ";";
			}
			else {
				(*out) << "gl_FragColor" << " = " << getReference(inst.operands[1]) << ";";
			}
		}
		else {
			output(out);
			if (compositeInserts.find(inst.operands[1]) != compositeInserts.end()) {
				(*out) << getReference(inst.operands[0]) << indexName(types[inst.operands[0]], compositeInserts[inst.operands[1]]) << " = " << getReference(inst.operands[1]) << ";";
			}
			else {
				(*out) << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
			}
		}
		break;
	}
	case OpTypeVoid:
		break;
	case OpEntryPoint:
		entryPoint = inst.operands[1];
		break;
	case OpMemoryModel:
		break;
	case OpExtInstImport:
		break;
	case OpSource:
		break;
	case OpCapability:
		break;
	default:
		output(out);
		(*out) << "// Unknown operation " << inst.opcode;
		break;
	}
}
