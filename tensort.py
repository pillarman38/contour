import tensorrt as trt
print(trt.__version__)
assert trt.Builder(trt.Logger())