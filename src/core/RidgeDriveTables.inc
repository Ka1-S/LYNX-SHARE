// round tp172c6: the DRIVE section's models (generated from the tp172c fits: ana/nodefit.py, ana/tonefit.py, ana/cabfit.py, the amps' tone: ana/ampfit.py on the drive-0 sweeps;
// /mnt/e/lynx-work/tp172c). Per measured position: T (dBFS), p, k, o (dB), a (dB), c2, the bell's and the shelf's gain (dB).
// Order = enum DriveModel.
constexpr DriveNode kDrive0[] { // Saturate
    { -4.0f, 1.7f, 0.9906f, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f },
};
constexpr DriveNode kDrive1[] { // Console E
    { 26.0f, 2.0f, 0.9976f, 0.46f, 0.0f, 0.0f, -1.28f, -0.45f },
    { 18.5f, 2.4f, 0.996f, 0.59f, 0.0f, 0.0f, -1.28f, -0.45f },
    { 25.0f, 1.8f, 0.9935f, 0.81f, 0.0f, 0.0f, -1.28f, -0.45f },
    { 17.0f, 2.2f, 0.9895f, 1.16f, 0.0f, 0.0f, -1.28f, -0.45f },
    { 9.0f, 3.0f, 0.9834f, 1.72f, 0.0f, 0.0f, -1.28f, -0.45f },
};
constexpr DriveNode kDrive2[] { // Console G
    { 26.5f, 2.2f, 0.9976f, 0.1f, 0.0f, 0.0f, -1.4f, -0.17f },
    { 29.0f, 2.0f, 0.996f, 0.23f, 0.0f, 0.0f, -1.4f, -0.17f },
    { 27.5f, 2.0f, 0.9935f, 0.44f, 0.0f, 0.0f, -1.4f, -0.17f },
    { 29.5f, 1.8f, 0.9896f, 0.79f, 0.0f, 0.0f, -1.4f, -0.17f },
    { 12.0f, 3.4f, 0.9836f, 1.35f, 0.0f, 0.0f, -1.4f, -0.17f },
};
constexpr DriveNode kDrive3[] { // Console A
    { 29.0f, 1.5f, 1.0f, 0.423f, 0.0f, 0.000438f, -0.42f, 0.22f },
    { 29.0f, 1.5f, 1.0f, 0.425f, 0.0f, 0.000735f, -0.2f, 0.22f },
    { 29.0f, 1.5f, 1.0f, 0.43f, 0.0f, 0.00123f, 0.3f, 0.22f },
    { 29.0f, 1.5f, 1.0f, 0.438f, 0.0f, 0.00207f, 0.88f, 0.22f },
    { 29.0f, 1.5f, 1.0f, 0.452f, 0.0f, 0.00347f, 1.64f, 0.22f },
};
constexpr DriveNode kDrive4[] { // Console N
    { 12.68f, 2.0f, 1.0f, 0.077f, 0.0f, 7.5e-05f, 5.45f, 0.23f },
    { 10.49f, 2.0f, 1.0f, 0.149f, 0.0f, 0.000125f, 5.45f, 0.23f },
    { 8.31f, 2.0f, 1.0f, 0.268f, 0.0f, 0.00021f, 5.45f, 0.23f },
    { 6.19f, 2.0f, 1.0f, 0.464f, 0.0f, 0.00035f, 5.45f, 0.23f },
    { 4.11f, 2.0f, 1.0f, 0.783f, 0.0f, 0.00057f, 5.45f, 0.23f },
};
constexpr DriveNode kDrive5[] { // Console Y
    { 29.0f, 2.0f, 0.99979f, 0.175f, 0.0f, 0.0f, -0.5f, 0.23f },
    { 29.0f, 2.0f, 0.99965f, 0.159f, 0.0f, 0.0f, -0.5f, 0.38f },
    { 29.0f, 2.0f, 0.99941f, 0.134f, 0.0f, 0.0f, -0.5f, 0.62f },
    { 29.0f, 2.0f, 0.99902f, 0.094f, 0.0f, 0.0f, -0.5f, 1.02f },
    { 29.0f, 2.0f, 0.99833f, 0.038f, 0.0f, 0.0f, -0.5f, 1.65f },
};
constexpr DriveNode kDrive6[] { // Tube
    { 29.0f, 0.9f, 1.0f, -0.48f, 3.0f, 0.0f, 0.15f, 0.35f },
    { 12.0f, 1.2f, 1.0f, -0.23f, 3.0f, 0.0f, 0.15f, 0.35f },
    { 2.0f, 1.5f, 1.0f, 0.19f, 3.0f, 0.0f, 0.15f, 0.35f },
    { 0.0f, 1.5f, 1.0f, 0.84f, 3.0f, 0.0f, 0.15f, 0.35f },
    { -2.0f, 1.5f, 1.0f, 1.83f, 3.0f, 0.0f, 0.15f, 0.35f },
};
// round tp172c9: the amps' nodes refitted on the stair harmonics de-embedded from the cabinet tone (the stage filters the
// curve's harmonics with it; the c6 fit read them raw, so H2 / H3 came out ~4 dB high)
constexpr DriveNode kDrive7[] { // Guitar amp (tp172c9: a 4 kHz low-pass ahead of the curve, see kDriveModels)
    { -5.0f, 1.9f, 0.769f, -2.22f, 0.0f, 0.0f, 16.34f, -7.17f }, // (fitted with the +30 dB pre-amp stair too: its ceiling)
    { -13.0f, 1.9f, 0.769f, 1.08f, 1.5f, 0.0f, 16.34f, -7.17f },
    { -25.0f, 2.0f, 0.770f, 4.37f, 3.0f, 0.0f, 16.34f, -7.17f },
};
constexpr DriveNode kDrive8[] { // Bass amp (tp172c9: a +17 dB 1.27 kHz pre-emphasis shelf and the high-pass ahead of the curve, one tone for every
// drive; the shelf after it = what drive 0 compresses away)
    { -7.5f, 2.0f, 0.669f, 6.46f, 3.0f, 0.0f, 7.50f, 3.5f },
    { -24.0f, 2.1f, 0.670f, 12.85f, 3.0f, 0.0f, 7.50f, 3.5f },
    { -39.0f, 2.0f, 0.693f, 18.92f, 3.0f, 0.0f, 7.50f, 3.5f },
};
// lo, hi, positions, nodes, bell Hz / Q, high shelf Hz, low-pass Hz / Q / sections (0 = none), high-pass Hz / Q,
// second bell Hz / Q / dB, first-order all-pass Hz (0 = none; the phase the originals' low end showed), the parts ahead of the
// curve (mask, low-pass Hz / Q, shelf dB; round tp172c9) (the tone is normalised to 0 dB at 1 kHz, where the curve's gain was measured)
constexpr DriveModelDef kDriveModels[driveModels] {
    { -24.0, 24.0, 1, kDrive0, 1000.0, 0.7, 10000.0, 0.0, 0.707, 0, 0.0, 0.707, 1000.0, 0.7, 0.0, 0.0, 0, 0.0, 0.707, 0.0 },        // Saturate
    { 0.0, 18.0, 5, kDrive1, 10.0, 0.4, 6724.0, 0.0, 0.707, 0, 0.0, 0.707, 1000.0, 0.7, 0.0, 0.0, 0, 0.0, 0.707, 0.0 },             // Console E
    { 0.0, 18.0, 5, kDrive2, 10.0, 0.72, 9010.0, 0.0, 0.707, 0, 0.0, 0.707, 1000.0, 0.7, 0.0, 0.0, 0, 0.0, 0.707, 0.0 },            // Console G
    { 0.0, 18.0, 5, kDrive3, 23.0, 0.93, 1500.0, 0.0, 0.707, 0, 0.0, 0.707, 1000.0, 0.7, 0.0, 28.4, 0, 0.0, 0.707, 0.0 },            // Console A
    { 0.0, 18.0, 5, kDrive4, 15.4, 0.41, 4592.0, 0.0, 0.707, 0, 0.0, 0.707, 1000.0, 0.7, 0.0, 63.8, 0, 0.0, 0.707, 0.0 },            // Console N
    { 0.0, 18.0, 5, kDrive5, 19.0, 1.5, 2390.0, 0.0, 0.707, 0, 0.0, 0.707, 1000.0, 0.7, 0.0, 0.0, 0, 0.0, 0.707, 0.0 },             // Console Y
    { 0.0, 18.0, 5, kDrive6, 66.0, 1.0, 1705.0, 0.0, 0.707, 0, 0.0, 0.707, 1000.0, 0.7, 0.0, 0.0, 0, 0.0, 0.707, 0.0 },             // Tube
    { 0.0, 11.0, 3, kDrive7, 48.6, 0.19, 553.0, 2416.0, 1.10, 1, 47.8, 0.34, 4710.0, 0.49, 15.0, 0.0, 0, 4000.0, 0.85, 0.0 },         // Guitar amp
    { 0.0, 11.0, 3, kDrive8, 55.5, 0.39, 1273.0, 5187.0, 0.99, 2, 30.0, 0.28, 1198.0, 1.51, 4.39, 0.0, 8, 0.0, 0.707, 16.98 },      // Bass amp
};
