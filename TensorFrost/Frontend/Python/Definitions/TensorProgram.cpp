#include <utility>
#include <vector>

#include <Frontend/Python/PyTensor.h>

namespace TensorFrost {

void TensorProgramDefinition(py::module& m,
                             py::class_<TensorProgram>& tensor_program) {
	m.def(
	    "program",
	    [](const py::function& py_evaluate) {
		    return new TensorProgram([py_evaluate]() -> Tensors {
			    py::gil_scoped_acquire acquire;
			    py::object result = py_evaluate();
			    auto py_outputs = py::cast<vector<PyTensor>>(result);
			    Tensors outputs = Tensors();
			    for (PyTensor output : py_outputs) {
				    outputs.push_back(&output.Get());
			    }
			    return outputs;
		    });
	    },
	    "Compile a TensorProgram from a python function");

	tensor_program.def(
	    "__call__",
	    [](TensorProgram& /*program*/, py::list py_inputs) {
		    Tensors inputs = TensorsFromList(py_inputs);
		    Tensors outputs = TensorProgram::Evaluate(inputs);

		    std::vector<PyTensor> outputs2 = std::vector<PyTensor>();
		    for (const Tensor* output : outputs) {
			    outputs2.push_back(PT(*output));
		    }
		    return py::cast(outputs2);
	    },
	    "Evaluate the TensorProgram with the given inputs");

	tensor_program.def("list_operations",
	                   [](TensorProgram& program, bool compact) {
		std::string listing = "List of operations:\n";
		listing += GetOperationListing(program.ir, compact);
		py::str result = py::str(listing);
		py::print(result);
	}, py::arg("compact") = true);

	tensor_program.def("kernel_hlsl", [](TensorProgram& program) {
		std::string hlsl = GenerateHLSL(program.ir);
		py::str result = py::str(hlsl);
		py::print(result);
	});
}

}  // namespace TensorFrost
