# Basic Pitch model (Spotify, Apache-2.0)

`nmp.onnx.inc` is `basic_pitch/saved_models/icassp_2022/nmp.onnx` from https://github.com/spotify/basic-pitch at commit
fa5997af0a8210982619003269994a1be25eddf3 (230 444 bytes, SHA-256
2c3c1d144bfa61ad236e92e169c13535c880469a12a047d4e73451f2c059a0ec), unmodified, written as a C++ byte array by
`python3 tools/make_model_inc.py nmp.onnx basicPitchModel third_party/basic-pitch/nmp.onnx.inc`.
`LICENSE` and `NOTICE` are the repository's own. Used by "Convert to MIDI" (src/ui/AudioToMidi.cpp).

Graph: input `serving_default_input_2:0` [batch, 43844, 1] (22.05 kHz mono, 2 s minus one hop); outputs
`StatefulPartitionedCall:1` note [batch, 172, 88], `:2` onset [batch, 172, 88], `:0` contour [batch, 172, 264].
