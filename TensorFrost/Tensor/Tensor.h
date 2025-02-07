#pragma once

#include <algorithm>
#include <list>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include <math.h>

#include "IR/IR.h"
#include "Utility/Utility.h"

namespace TensorFrost {

using Tensors = vector<const Tensor*>;

class Tensor {
 private:
	static IR* evaluation_context_ir_;

	static Tensor& CreateNode(DataType type, Arguments args, string name) {
		if (evaluation_context_ir_ == nullptr) {
			throw std::runtime_error(
			    "Evaluation context has not been set. Are you doing operations "
			    "outside a TensorProgram?");
		}

		auto* tensor = new Tensor(type);
		tensor->node_ = evaluation_context_ir_->AddNode(tensor, std::move(args),
		                                               std::move(name));
		return *tensor;
	}

	static void AddArgument(Arguments& arguments, const Tensor* tensor,
	                        Arg::Type type, int index = 0) {
		arguments.emplace_back(type, tensor->node_->GetLable(), index);
	}

	static void AddArguments(Arguments& arguments, const Tensors& tensors,
	                         Arg::Type type) {
		for (int i = 0; i < tensors.size(); i++) {
			AddArgument(arguments, tensors[i], type, i);
		}
	}

	static void AddArguments(Arguments& arguments, const Arguments& toadd) {
		for (const auto& i : toadd) {
			arguments.push_back(i);
		}
	}

	static pair<Operation, DataType> GetOperation(const string& name,
	                                              const Tensors& tensors) {
		vector<DataType> input_types = vector<DataType>();
		for (const auto& tensor : tensors) {
			input_types.push_back(tensor->type);
		}

		const Operation& operation = FindOperation(name);

		// check if input is valid
		if (!operation.IsInputValid(input_types)) {
			string error = "Input types ";
			for (const auto& type : input_types) {
				error += DataTypeToString(type) + ", ";
			}
			error += "are not valid for operation " + name;
			throw std::runtime_error(error);
		}

		DataType output_type = operation.GetOutputType(input_types);

		return pair<Operation, DataType>(operation, output_type);
	}

	template <typename... Args>
	static Tensor& Op(std::string op, const Args*... args) {
		op = RemoveSpaces(op);

		if (op.empty()) {
			throw std::runtime_error("Operation name cannot be empty");
		}

		// convert the parameter pack to a std::vector
		Tensors tensors = {args...};

		// get the operation and output type
		pair<Operation, DataType> operation = GetOperation(op, tensors);
		DataType output_type = operation.second;

		// create argument list
		Arguments arguments = Arguments();

		AddArguments(arguments, tensors, Arg::Type::Input);

		// get an input node that has shape arguments
		Arguments shape_arguments;
		for (const Tensor* tensor : tensors) {
			shape_arguments = tensor->node_->GetArguments(Arg::Type::Shape);
			if (!shape_arguments.empty()) {
				break;
			}
		}

		AddArguments(arguments, shape_arguments);

		return CreateNode(output_type, arguments, op);
	}

	template <typename... Args>
	static Tensor& MemoryOp(string op, const Tensor* memory,
	                        const Tensors indices, const Args*... args) {
		op = RemoveSpaces(op);

		if (op.empty()) {
			throw std::runtime_error("Memory operation name cannot be empty");
		}

		// check if indices are all integers
		for (const Tensor* index : indices) {
			if (index->type != DataType::Int) {
				throw std::runtime_error("Tensor indices must be integers");
			}
		}

		// convert the parameter pack to a std::vector
		Tensors tensors = {args...};

		// get the operation and output type
		pair<Operation, DataType> operation = GetOperation(op, tensors);
		DataType output_type = operation.second;

		// create argument list
		Arguments arguments = Arguments();

		AddArgument(arguments, memory, Arg::Type::Memory);
		AddArguments(arguments, tensors, Arg::Type::Input);
		AddArguments(arguments, indices, Arg::Type::Index);
		Node* shape_source = (!indices.empty()) ? indices[0]->node_ : memory->node_;
		AddArguments(arguments, shape_source->GetArguments(Arg::Type::Shape));

		if (op == "load") output_type = memory->type;

		return CreateNode(output_type, arguments, op);
	}

	static Tensor& Static(string op, const Arguments& shape,
	                      const DataType type) {
		op = RemoveSpaces(op);

		if (op.empty()) {
			throw std::runtime_error("Static operation name cannot be empty");
		}

		const Operation& operation = FindOperation(op);
		// check if output is valid
		if (!operation.IsOutputValid(type)) {
			throw std::runtime_error("Type " + DataTypeToString(type) +
			                         " is not valid for operation " + op);
		}
		Arguments arguments = Arguments();
		AddArguments(arguments, shape);
		return CreateNode(type, arguments, op);
	}

	static Tensor& Static(const string& op, const Tensors& shape,
	                      const DataType type) {
		Arguments arguments = Arguments();
		AddArguments(arguments, shape, Arg::Type::Shape);
		return Static(op, arguments, type);
	}

	static Tensor& Static(const string& op, const DataType type) {
		return Static(op, Arguments(), type);
	}

 public:
	static void SetEvaluationContext(IR* ir) {
		if (evaluation_context_ir_ != nullptr && ir != nullptr) {
			throw std::runtime_error("Evaluation context change is forbidden.");
		}
		evaluation_context_ir_ = ir;
	}

	[[nodiscard]] string GetConstantString() const;

	Node* node_ = nullptr;
	DataType type = DataType::Float;
	std::vector<uint> data;

	// Main constructor
	explicit Tensor(DataType type) { this->type = type; }

	static Tensor* GetCopy(const Tensor& other, Arguments args) {
		Tensor* copy = &CreateNode(other.type, std::move(args), other.node_->name);
		copy->data = other.data;
		return copy;
	}

	void SetMemoryType(MemoryType memory_type, int index = 0) const {
		node_->SetMemoryType(memory_type, index);
	}

	[[nodiscard]] int GetDimension() const {
		// find max dimension
		int max_dim = -1;

		for (const auto& input : node_->inputs_) {
			if (input.type_ == Arg::Type::Shape) {
				max_dim = std::max(max_dim, input.index_);
			}
		}

		return max_dim + 1;
	}

	[[nodiscard]] Tensors GetShape() const {
		Tensors result = Tensors();
		// get max dimension
		int max_dim = -1;
		for (const auto& input : node_->inputs_) {
			if (input.type_ == Arg::Type::Shape) {
				max_dim = std::max(max_dim, input.index_);
			}
		}

		if (max_dim == -1) {
			return result;
		}

		// resize result
		result.resize(max_dim + 1);
		for (int i = 0; i <= max_dim; i++) {
			result[i] = nullptr;
		}
		// fill result
		for (const auto& input : node_->inputs_) {
			if (input.type_ == Arg::Type::Shape) {
				result[input.index_] = input.from_->get()->GetTensor();
			}
		}
		// if there are any missing dimensions, fill them with 1
		Tensor& one = Constant(1);
		for (int i = 0; i <= max_dim; i++) {
			if (result[i] == nullptr) {
				result[i] = &one;
			}
		}
		return result;
	}

	[[nodiscard]] vector<int> TryGetShape() const {
		vector<int> result = vector<int>();
		// get max dimension
		int max_dim = -1;
		for (const auto& input : node_->inputs_) {
			if (input.type_ == Arg::Type::Shape) {
				max_dim = std::max(max_dim, input.index_);
			}
		}

		if (max_dim == -1) {
			return result;
		}

		// resize result
		result.resize(max_dim + 1);
		for (int i = 0; i <= max_dim; i++) {
			result[i] = 1;
		}
		// fill result
		for (const auto& input : node_->inputs_) {
			if (input.type_ == Arg::Type::Shape) {
				result[input.index_] = AsInt(input.from_->get()->GetTensor()->data[0]);
			}
		}
		return result;
	}

	// tensor factory methods
	static Tensors GetConstantShape(const vector<int>& shape) {
		Tensors result = Tensors();
		for (int i : shape) {
			result.push_back(&Constant(i));
		}
		return result;
	}

	static Tensor& Constant(float value) {
		Tensor& output = Static("const", DataType::Float);
		output.data = std::vector<uint>(1, AsUint(value));
		return output;
	}
	static Tensor& Constant(int value) {
		Tensor& output = Static("const", DataType::Int);
		output.data = std::vector<uint>(1, AsUint(value));
		return output;
	}
	static Tensor& Constant(uint value) {
		Tensor& output = Static("const", DataType::Uint);
		output.data = std::vector<uint>(1, value);
		return output;
	}

	static Tensor& Constant(const vector<int>& shape, float* data) {
		Tensor& output = Static("memory", GetConstantShape(shape), DataType::Float);
		output.SetMemoryType(MemoryType::Constant);
		int data_count = GetSize(shape);
		for (int i = 0; i < data_count; i++) {
			output.data.push_back(AsUint(data[i]));
		}
		return output;
	}

	static Tensor& Constant(const Tensors& shape, float value) {
		Arguments arguments = Arguments();
		AddArguments(arguments, shape, Arg::Type::Shape);
		Tensor& output = Static("const", arguments, DataType::Float);
		output.data = std::vector<uint>(1, AsUint(value));
		return output;
	}
	static Tensor& Constant(const vector<int>& shape, float value) {
		return Constant(GetConstantShape(shape), value);
	}
	static Tensor& Constant(const Tensors& shape, int value) {
		Arguments arguments = Arguments();
		AddArguments(arguments, shape, Arg::Type::Shape);
		Tensor& output = Static("const", arguments, DataType::Int);
		output.data = std::vector<uint>(1, AsUint(value));
		return output;
	}
	static Tensor& Constant(const vector<int>& shape, int value) {
		return Constant(GetConstantShape(shape), value);
	}
	static Tensor& Constant(const Tensors& shape, uint value) {
		Arguments arguments = Arguments();
		AddArguments(arguments, shape, Arg::Type::Shape);
		Tensor& output = Static("const", arguments, DataType::Uint);
		output.data = std::vector<uint>(1, value);
		return output;
	}
	static Tensor& Constant(const vector<int>& shape, uint value) {
		return Constant(GetConstantShape(shape), value);
	}

	static Tensor& Memory(const DataType type) { return Static("memory", type); }
	static Tensor& Memory(const Tensors& shape,
	                      const DataType type = DataType::Float) {
		return Static("memory", shape, type);
	}
	static Tensor& Memory(const Arguments& shape,
	                      const DataType type = DataType::Float) {
		return Static("memory", shape, type);
	}
	static Tensor& Input(const DataType type = DataType::Float) {
		Tensor& output = Memory(type);
		output.SetMemoryType(MemoryType::Input);
		return output;
	}
	static Tensor& Input(const Tensors& shape,
	                     const DataType type = DataType::Float) {
		Tensor& output = Memory(shape, type);
		output.SetMemoryType(MemoryType::Input);
		return output;
	}
	static Tensors GetInputShape(const vector<int>& shape) {
		Tensors result = Tensors();
		for (int i : shape) {
			if (i < 0) {
				result.push_back(&Memory(DataType::Int));
			} else {
				result.push_back(&Constant(i));
			}
		}
		return result;
	}
	static Tensor& Input(const vector<int>& shape,
	                     const DataType type = DataType::Float) {
		return Input(GetInputShape(shape), type);
	}

	static Tensor& Index(const Tensors& shape, int dim) {
		Tensor& output = Static("dim_id", shape, DataType::Int);
		output.data = std::vector<uint>(1, dim);
		output.type = DataType::Int;
		return output;
	}

	static Tensor& Index(const vector<int>& shape, int dim) {
		return Index(GetConstantShape(shape), dim);
	}

	static Tensor& ThreadIndex(const Tensors& shape) {
		Tensor& output = Static("thread_id", shape, DataType::Int);
		output.type = DataType::Int;
		return output;
	}

	[[nodiscard]] Tensor& ThreadIndex() const {
		Tensor& output = Static(
		    "thread_id", node_->GetArguments(Arg::Type::Shape), DataType::Int);
		output.type = DataType::Int;
		return output;
	}

	static Tensor& Load(const Tensor& tensor,
	                    const Tensors& indices = Tensors()) {
		return MemoryOp("load", &tensor, indices);
	}

	[[nodiscard]] Tensor& Index(int dim) const {
		Tensor& output = Static("dim_id", node_->GetArguments(Arg::Type::Shape),
		                        DataType::Int);
		output.data = std::vector<uint>(1, dim);
		output.type = DataType::Int;
		return output;
	}

	static Tensor& Store(const Tensor& tensor, const Tensor& value,
	                     const Tensors& indices = Tensors()) {
		return MemoryOp("store", &tensor, indices, &value);
	}

	static void ScatterAdd(const Tensor& tensor, const Tensor& value,
	                       const Tensors& indices) {
		MemoryOp("InterlockedAdd", &tensor, indices, &value);
	}

	static void ScatterMax(const Tensor& tensor, const Tensor& value,
	                       const Tensors& indices) {
		MemoryOp("InterlockedMax", &tensor, indices, &value);
	}

	static void ScatterMin(const Tensor& tensor, const Tensor& value,
	                       const Tensors& indices) {
		MemoryOp("InterlockedMin", &tensor, indices, &value);
	}

	static void ScatterOr(const Tensor& tensor, const Tensor& value,
	                      const Tensors& indices) {
		MemoryOp("InterlockedOr", &tensor, indices, &value);
	}

	static void ScatterAnd(const Tensor& tensor, const Tensor& value,
	                       const Tensors& indices) {
		MemoryOp("InterlockedAnd", &tensor, indices, &value);
	}

	static void ScatterXor(const Tensor& tensor, const Tensor& value,
	                       const Tensors& indices) {
		MemoryOp("InterlockedXor", &tensor, indices, &value);
	}

	static Tensor& Sum(const Tensor& tensor, const int dim) {
		Tensor& res = Op("sum", &tensor);
		res.data = std::vector<uint>(1, dim);
		return res;
	}

	static void Loop(const Tensor& start, const Tensor& end, const Tensor& step,
	                 const std::function<void(const Tensor&)>& body) {
		// create the loop
		Tensor& loop = Op("loop_begin", &start, &end, &step);

		// create the body
		body(loop);

		// end the loop
		Op("loop_end", &loop);
	}

	// destructor
	~Tensor() = default;

	Tensor& operator-() const { return Op("neg", this); }
	Tensor& operator!() const { return Op("not", this); }
	Tensor& operator~() const { return Op("bnot", this); }

	[[nodiscard]] bool isConstantEqualTo(float value) const {
		if (node_->name != "const") {
			return false;
		}
		switch (type) {
			case DataType::Float:
				return AsFloat(data[0]) == value;
			case DataType::Int:
				return AsInt(data[0]) == value;
			case DataType::Uint:
				return data[0] == value;
			default:
				throw std::runtime_error("Unexpected type in isConstantEqualTo");
		}
	}

	Tensor& operator+(const Tensor& other) const {
		// if this or other is a zero constant, return the other
		if (isConstantEqualTo(0.0)) {
			return const_cast<Tensor&>(other);
		}
		if (other.isConstantEqualTo(0.0)) {
			return const_cast<Tensor&>(*this);
		}
		// if both are constants, return the difference
		if (this->node_->name == "const" && other.node_->name == "const") {
			switch (type) {
				case DataType::Float:
					return Constant(AsFloat(this->data[0]) + AsFloat(other.data[0]));
				case DataType::Int:
					return Constant(AsInt(this->data[0]) + AsInt(other.data[0]));
				case DataType::Uint:
					return Constant(this->data[0] + other.data[0]);
			}
		}
		return Op("add", this, &other);
	}

	Tensor& operator-(const Tensor& other) const {
		// if this or other is a zero constant, return the other
		if (isConstantEqualTo(0.0)) {
			return -const_cast<Tensor&>(other);
		}
		if (other.isConstantEqualTo(0.0)) {
			return const_cast<Tensor&>(*this);
		}
		// if both are constants, return the difference
		if (this->node_->name == "const" && other.node_->name == "const") {
			switch (type) {
				case DataType::Float:
					return Constant(AsFloat(this->data[0]) - AsFloat(other.data[0]));
				case DataType::Int:
					return Constant(AsInt(this->data[0]) - AsInt(other.data[0]));
				case DataType::Uint:
					return Constant(this->data[0] - other.data[0]);
			}
		}
		return Op("sub", this, &other);
	}

	Tensor& operator*(const Tensor& other) const {
		// if this or other is a zero constant, return zero
		if (isConstantEqualTo(0.0) || other.isConstantEqualTo(0.0)) {
			switch (type) {
				case DataType::Float:
					return Constant(0.0F);
				case DataType::Int:
					return Constant(0);
				case DataType::Uint:
					return Constant(0U);
			}
		}
		// if this or other is a one constant, return the other
		if (isConstantEqualTo(1.0)) {
			return const_cast<Tensor&>(other);
		}
		if (other.isConstantEqualTo(1.0)) {
			return const_cast<Tensor&>(*this);
		}
		if (this->node_->name == "const" && other.node_->name == "const") {
			switch (type) {
				case DataType::Float:
					return Constant(AsFloat(this->data[0]) * AsFloat(other.data[0]));
				case DataType::Int:
					return Constant(AsInt(this->data[0]) * AsInt(other.data[0]));
				case DataType::Uint:
					return Constant(this->data[0] * other.data[0]);
			}
		}
		return Op("mul", this, &other);
	}

	Tensor& operator/(const Tensor& other) const {
		// if this is a zero constant, return zero
		if (isConstantEqualTo(0.0)) {
			switch (type) {
				case DataType::Float:
					return Constant(0.0F);
				case DataType::Int:
					return Constant(0);
				case DataType::Uint:
					return Constant(0U);
			}
		}
		// if other is a one constant, return this
		if (other.isConstantEqualTo(1.0)) {
			return const_cast<Tensor&>(*this);
		}
		// if both are constants, return the ratio
		if (this->node_->name == "const" && other.node_->name == "const") {
			switch (type) {
				case DataType::Float:
					return Constant(AsFloat(this->data[0]) / AsFloat(other.data[0]));
				case DataType::Int:
					return Constant(AsInt(this->data[0]) / AsInt(other.data[0]));
				case DataType::Uint:
					return Constant(this->data[0] / other.data[0]);
			}
		}
		return Op("div", this, &other);
	}

	Tensor& operator%(const Tensor& other) const {
		return Op("mod", this, &other);
	}

	Tensor& operator>(const Tensor& other) const {
		return Op("gt", this, &other);
	}

	Tensor& operator<(const Tensor& other) const {
		return Op("lt", this, &other);
	}

	Tensor& operator>=(const Tensor& other) const {
		return Op("gte", this, &other);
	}

	Tensor& operator<=(const Tensor& other) const {
		return Op("lte", this, &other);
	}

	Tensor& operator==(const Tensor& other) const {
		return Op("eq", this, &other);
	}

	Tensor& operator!=(const Tensor& other) const {
		return Op("neq", this, &other);
	}

	Tensor& operator&&(const Tensor& other) const {
		return Op("and", this, &other);
	}

	Tensor& operator||(const Tensor& other) const {
		return Op("or", this, &other);
	}

	Tensor& operator&(const Tensor& other) const {
		return Op("band", this, &other);
	}

	Tensor& operator|(const Tensor& other) const {
		return Op("bor", this, &other);
	}

	Tensor& operator^(const Tensor& other) const {
		return Op("bxor", this, &other);
	}

	Tensor& operator<<(const Tensor& other) const {
		return Op("blshift", this, &other);
	}

	Tensor& operator>>(const Tensor& other) const {
		return Op("brshift", this, &other);
	}

	void operator=(const Tensor& other) = delete;
	
	static Tensor& ifcond(const Tensor& condition, const Tensor& ifTrue,
	                      const Tensor& ifFalse) {
		return Op("cond", &condition, &ifTrue, &ifFalse);
	}

	static Tensor& sin(const Tensor& x) { return Op("sin", &x); }
	static Tensor& cos(const Tensor& x) { return Op("cos", &x); }
	static Tensor& tan(const Tensor& x) { return Op("tan", &x); }
	static Tensor& asin(const Tensor& x) { return Op("asin", &x); }
	static Tensor& acos(const Tensor& x) { return Op("acos", &x); }
	static Tensor& atan(const Tensor& x) { return Op("atan", &x); }
	static Tensor& sinh(const Tensor& x) { return Op("sinh", &x); }
	static Tensor& cosh(const Tensor& x) { return Op("cosh", &x); }
	static Tensor& tanh(const Tensor& x) { return Op("tanh", &x); }
	static Tensor& asinh(const Tensor& x) { return Op("asinh", &x); }
	static Tensor& acosh(const Tensor& x) { return Op("acosh", &x); }
	static Tensor& atanh(const Tensor& x) { return Op("atanh", &x); }
	static Tensor& exp(const Tensor& x) { return Op("exp", &x); }
	static Tensor& log(const Tensor& x) { return Op("log", &x); }
	static Tensor& log2(const Tensor& x) { return Op("log2", &x); }
	static Tensor& exp2(const Tensor& x) { return Op("exp2", &x); }
	static Tensor& sqrt(const Tensor& x) { return Op("sqrt", &x); }
	static Tensor& sqr(const Tensor& x) { return Op("sqr", &x); }
	static Tensor& rsqrt(const Tensor& x) { return Op("rsqrt", &x); }
	static Tensor& rcp(const Tensor& x) { return Op("rcp", &x); }
	static Tensor& abs(const Tensor& x) { return Op("abs", &x); }
	static Tensor& sign(const Tensor& x) { return Op("sign", &x); }
	static Tensor& floor(const Tensor& x) { return Op("floor", &x); }
	static Tensor& ceil(const Tensor& x) { return Op("ceil", &x); }
	static Tensor& round(const Tensor& x) { return Op("round", &x); }
	static Tensor& trunc(const Tensor& x) { return Op("trunc", &x); }
	static Tensor& frac(const Tensor& x) { return Op("frac", &x); }

	static Tensor& tofloat(const Tensor& x) { return Op("float", &x); }
	static Tensor& toint(const Tensor& x) { return Op("int", &x); }
	static Tensor& touint(const Tensor& x) { return Op("uint", &x); }

	static Tensor& clamp(const Tensor& x, const Tensor& min, const Tensor& max) {
		return Op("clamp", &x, &min, &max);
	}

	static Tensor& pow(const Tensor& x, const Tensor& y) {
		return Op("pow", &x, &y);
	}

	static Tensor& min(const Tensor& x, const Tensor& y) {
		return Op("min", &x, &y);
	}

	static Tensor& max(const Tensor& x, const Tensor& y) {
		return Op("max", &x, &y);
	}

	static Tensor& mod(const Tensor& x, const Tensor& y) {
		return Op("mod", &x, &y);
	}

	static Tensor& atan2(const Tensor& x, const Tensor& y) {
		return Op("atan2", &x, &y);
	}

	static Tensor& lerp(const Tensor& x, const Tensor& y, const Tensor& a) {
		return Op("lerp", &x, &y, &a);
	}

	static Tensor& fma(const Tensor& x, const Tensor& y, const Tensor& z) {
		return Op("fma", &x, &y, &z);
	}
};

}  // namespace TensorFrost
