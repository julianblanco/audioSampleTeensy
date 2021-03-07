#!/usr/bin/env python3
import sys
import struct

import numpy as np
from scipy.io import wavfile
import matplotlib.pyplot as plt
if len(sys.argv) < 3:
    print("usage: decode.py sample-data output-file")
    sys.exit(1)

with open(sys.argv[1], "rb") as filp:
    data = filp.read()

samples = [struct.unpack("<I", data[i:i+4])[0] for i in range(0, len(data), 4)]
samples = [(float(sample)*2.0)/1024.0 - 1.0 for sample in samples]

with open(sys.argv[2] + ".csv", "w") as filp:
    filp.write(",".join([str(sample) for sample in samples]))

np_samples = np.array(samples)
wavfile.write(sys.argv[2] , 5000, np_samples.astype(np.float32))

plt.plot( np_samples)
plt.show()