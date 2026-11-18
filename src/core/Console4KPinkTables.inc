// round 190lb: the SSL 4K G "Pink" EQ colour, measured black-box (docs/analise/BLACKBOX_rodada190lb.md §2): one
// second-order section per band fitted on each impulse render of the reference's trial grid (tp172c K4G_ir, 48 kHz;
// /mnt/e/lynx-work/blackbox190lb/lab fitpink.py + mktab.py). Shelves: [dial frequency node][|dial gain| node] =
// { f0 Hz, realised |gain| dB, Q } (LF: boost and cut averaged; HF: the cuts, its boosts reached the ceiling).
// Bells: [dial Q node][|dial gain| node] = { frequency / the band's max-gain frequency, |gain| dB, Q } at the 1 kHz /
// 3 kHz dial, and the max-gain frequency per dial frequency node (dial Q 3.0).
constexpr float pinkLfDial[5] = { 30.0f, 66.7f, 200.0f, 366.7f, 450.0f };
constexpr float pinkLfGain[3] = { 5.4f, 12.6f, 18.0f };
constexpr float pinkLf[5][3][3] = {
    { { 129.4436f, 3.6443f, 0.5420f }, { 95.6153f, 11.4595f, 0.5108f }, { 63.8761f, 20.0181f, 0.4583f } },
    { { 291.4852f, 3.5920f, 0.5940f }, { 223.6725f, 11.1812f, 0.5618f }, { 160.2649f, 19.5305f, 0.4910f } },
    { { 529.5481f, 3.5587f, 0.6291f }, { 439.7438f, 10.9483f, 0.6001f }, { 359.5316f, 18.5867f, 0.5483f } },
    { { 696.8536f, 3.5143f, 0.6512f }, { 601.2223f, 10.7794f, 0.6301f }, { 536.5501f, 18.1607f, 0.5775f } },
    { { 810.0558f, 3.4814f, 0.6781f }, { 735.1726f, 10.6832f, 0.6405f }, { 708.7886f, 17.8823f, 0.5886f } },
};

constexpr float pinkHfDial[5] = { 1500.0f, 3000.0f, 8000.0f, 12670.0f, 16000.0f };
constexpr float pinkHfGain[3] = { 6.0f, 14.0f, 20.0f };
constexpr float pinkHf[5][3][3] = {
    { { 386.5041f, 3.7641f, 0.6828f }, { 522.4507f, 11.2195f, 0.6318f }, { 769.9304f, 19.8762f, 0.5380f } },
    { { 892.5907f, 3.2273f, 0.6822f }, { 1178.5056f, 9.9951f, 0.6505f }, { 1814.2699f, 19.1790f, 0.5582f } },
    { { 1757.9616f, 2.5271f, 0.6686f }, { 2262.8431f, 8.2558f, 0.6724f }, { 3573.2044f, 17.6096f, 0.5993f } },
    { { 2441.7883f, 2.1299f, 0.6557f }, { 3083.9717f, 7.1671f, 0.6849f }, { 4846.8985f, 16.1134f, 0.6384f } },
    { { 3056.7536f, 1.8532f, 0.6415f }, { 3802.1930f, 6.3518f, 0.6937f }, { 5885.6442f, 14.7249f, 0.6769f } },
};

constexpr float pinkQNode[3] = { 0.5f, 1.5f, 3.0f };
constexpr float pinkMidGain[3] = { 6.6f, 15.4f, 22.0f };
constexpr float pinkLmf[3][3][3] = {
    { { 0.8487f, 6.3496f, 0.1823f }, { 0.9047f, 14.8104f, 0.1883f }, { 0.9908f, 21.4502f, 0.2138f } },
    { { 0.8531f, 5.7956f, 0.4201f }, { 0.9113f, 13.9763f, 0.4431f }, { 0.9995f, 21.2985f, 0.5331f } },
    { { 0.8537f, 5.6312f, 0.5509f }, { 0.9122f, 13.8988f, 0.5958f }, { 1.0005f, 22.0821f, 0.7588f } },
};

constexpr float pinkHmf[3][3][3] = {
    { { 0.8293f, 6.3700f, 0.1766f }, { 0.8970f, 15.2356f, 0.1910f }, { 0.9821f, 21.5911f, 0.2156f } },
    { { 0.8479f, 5.8175f, 0.4253f }, { 0.9097f, 14.3845f, 0.4558f }, { 0.9980f, 21.4725f, 0.5409f } },
    { { 0.8520f, 5.6491f, 0.5575f }, { 0.9108f, 13.9349f, 0.6052f }, { 1.0009f, 22.1089f, 0.7691f } },
};

constexpr float pinkLmfDial[5] = { 200.0f, 466.6f, 1000.0f, 1833.4f, 2500.0f };
constexpr float pinkLmfHz[5] = { 203.8f, 503.3f, 1080.4f, 1587.4f, 2067.9f };
constexpr float pinkHmfDial[5] = { 600.0f, 1033.4f, 3000.0f, 5500.0f, 7000.0f };
constexpr float pinkHmfHz[5] = { 608.1f, 1500.4f, 3209.8f, 4723.3f, 6148.2f };
constexpr double pinkDiv3 = 0.2422, pinkMult3 = 2.8778;
constexpr double pinkCeiling = 1.1108; // the reference's output ceiling (hard clip, both polarities)
