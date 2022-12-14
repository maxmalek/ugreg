static inline unsigned casefold_tabindex(unsigned x) { return (x ^ (x >> 9)) & 0xff; }
static const unsigned short casefold16_1_keys[1163] = {
0x0100, 0x0402, 0x0502, 0x1f0f, 0x2c16, 0x0200, 0x0403, 0x1e0e, 0x1f0e, 0x2c17, 0xa652, 0xa752, 0x0102, 0x0400, 0x0500, 0x1f0d,
0x2c14, 0x0202, 0x0401, 0x1e0c, 0x1f0c, 0x2c15, 0xa650, 0xa750, 0x0104, 0x0406, 0x0506, 0x1f0b, 0x2c12, 0x0204, 0x0407, 0x1e0a,
0x1f0a, 0x2c13, 0xa656, 0xa756, 0x0106, 0x0404, 0x0504, 0x1f09, 0x2c10, 0x0206, 0x0405, 0x1e08, 0x1f08, 0x2c11, 0xa654, 0xa754,
0x0108, 0x040a, 0x050a, 0x2c1e, 0x0208, 0x040b, 0x1e06, 0x2c1f, 0xa65a, 0xa75a, 0x010a, 0x0408, 0x0508, 0x2c1c, 0x020a, 0x0409,
0x1e04, 0x2c1d, 0xa658, 0xa758, 0x010c, 0x040e, 0x050e, 0x2c1a, 0x020c, 0x040f, 0x1e02, 0x2c1b, 0xa65e, 0xa75e, 0x010e, 0x040c,
0x050c, 0x2c18, 0x020e, 0x040d, 0x1e00, 0x2c19, 0xa65c, 0xa75c, 0x0110, 0x0412, 0x0512, 0x2c06, 0x0210, 0x0413, 0x1e1e, 0x2c07,
0xa642, 0xa742, 0x0112, 0x0410, 0x0510, 0x1f1d, 0x2c04, 0x0212, 0x0411, 0x1e1c, 0x1f1c, 0x2c05, 0xa640, 0xa740, 0x0114, 0x0416,
0x0516, 0x1f1b, 0x2c02, 0x0214, 0x0417, 0x1e1a, 0x1f1a, 0x2c03, 0xa646, 0xa746, 0x0116, 0x0414, 0x0514, 0x1f19, 0x2c00, 0x0216,
0x0415, 0x1e18, 0x1f18, 0x2c01, 0xa644, 0xa744, 0x0118, 0x041a, 0x051a, 0x2c0e, 0x0218, 0x041b, 0x1e16, 0x2c0f, 0xa64a, 0xa74a,
0x011a, 0x0418, 0x0518, 0x2c0c, 0x021a, 0x0419, 0x1e14, 0x2c0d, 0xa648, 0xa748, 0x011c, 0x041e, 0x051e, 0x2c0a, 0x021c, 0x041f,
0x1e12, 0x2c0b, 0xa64e, 0xa74e, 0x011e, 0x041c, 0x051c, 0x2c08, 0x021e, 0x041d, 0x1e10, 0x2c09, 0xa64c, 0xa74c, 0x0120, 0x0422,
0x0522, 0x1f2f, 0xab75, 0x0220, 0x0423, 0x1e2e, 0x1f2e, 0xab74, 0x0122, 0x0420, 0x0520, 0x1f2d, 0x2132, 0xab77, 0x0222, 0x0421,
0x1e2c, 0x1f2c, 0xab76, 0x0124, 0x0426, 0x0526, 0x1f2b, 0xab71, 0x0224, 0x0427, 0x1e2a, 0x1f2a, 0xab70, 0x0126, 0x0424, 0x0524,
0x1f29, 0xab73, 0x0226, 0x0425, 0x1e28, 0x1f28, 0xab72, 0x0128, 0x042a, 0x052a, 0xa77b, 0xab7d, 0x0228, 0x042b, 0x1e26, 0xab7c,
0x012a, 0x0428, 0x0528, 0xa779, 0xab7f, 0x022a, 0x0429, 0x1e24, 0xab7e, 0x012c, 0x042e, 0x052e, 0xab79, 0x022c, 0x042f, 0x1e22,
0xa77e, 0xab78, 0x012e, 0x042c, 0x052c, 0xa77d, 0xab7b, 0x022e, 0x042d, 0x1e20, 0xab7a, 0x0532, 0x1f3f, 0x2c26, 0x0230, 0x0533,
0x1e3e, 0x1f3e, 0x2c27, 0xa662, 0xa762, 0x0132, 0x1f3d, 0x2c24, 0x0232, 0x0531, 0x1e3c, 0x1f3c, 0x2c25, 0xa660, 0xa760, 0x0134,
0x0536, 0x1f3b, 0x2c22, 0x0537, 0x1e3a, 0x1f3a, 0x2c23, 0xa666, 0xa766, 0x0136, 0x0534, 0x1f39, 0x2126, 0x2c20, 0x0535, 0x1e38,
0x1f38, 0x2c21, 0xa664, 0xa764, 0x053a, 0x2c2e, 0x0139, 0x053b, 0x1e36, 0xa66a, 0xa76a, 0x023b, 0x0538, 0x212a, 0x2c2c, 0x013b,
0x023a, 0x0539, 0x1e34, 0x212b, 0x2c2d, 0xa668, 0xa768, 0x023d, 0x053e, 0x2c2a, 0x013d, 0x053f, 0x1e32, 0x2c2b, 0xa76e, 0x053c,
0x2c28, 0x013f, 0x023e, 0x053d, 0x1e30, 0x2c29, 0xa66c, 0xa76c, 0x0241, 0x0542, 0x0141, 0x0543, 0x1e4e, 0x0243, 0x0540, 0x1f4d,
0x0143, 0x0541, 0x1e4c, 0x1f4c, 0x0245, 0x0345, 0x0546, 0x1f4b, 0x0145, 0x0244, 0x0547, 0x1e4a, 0x1f4a, 0xff3a, 0x0544, 0x1f49,
0xff39, 0x0147, 0x0246, 0x0545, 0x1e48, 0x1f48, 0xff38, 0x054a, 0xff37, 0x0248, 0x054b, 0x1e46, 0xff36, 0x014a, 0x0548, 0xff35,
0x024a, 0x0549, 0x1e44, 0xff34, 0x014c, 0x054e, 0xff33, 0x024c, 0x054f, 0x1e42, 0xff32, 0x014e, 0x054c, 0xff31, 0x024e, 0x054d,
0x1e40, 0xff30, 0x0150, 0x0552, 0x1f5f, 0xff2f, 0x0553, 0x1e5e, 0xff2e, 0x0152, 0x0550, 0x1f5d, 0xff2d, 0x0551, 0x1e5c, 0xff2c,
0x0154, 0x0556, 0x1f5b, 0xff2b, 0x1e5a, 0xff2a, 0x0156, 0x0554, 0x1f59, 0xff29, 0x0555, 0x1e58, 0xff28, 0x0158, 0xff27, 0x1e56,
0xff26, 0x015a, 0xff25, 0x1e54, 0xff24, 0x015c, 0xff23, 0x1e52, 0xff22, 0x015e, 0xff21, 0x1e50, 0x0160, 0x0462, 0x1f6f, 0x1e6e,
0x1f6e, 0xa732, 0x0162, 0x0460, 0x1f6d, 0x1e6c, 0x1f6c, 0x2c75, 0x0164, 0x0466, 0x1f6b, 0x2c72, 0x1e6a, 0x1f6a, 0xa736, 0x0166,
0x0464, 0x1f69, 0x2c70, 0x1e68, 0x1f68, 0xa734, 0x0168, 0x046a, 0x2c7e, 0x1e66, 0x2c7f, 0xa73a, 0x016a, 0x0468, 0x1e64, 0xa738,
0x016c, 0x046e, 0x1e62, 0xa73e, 0x016e, 0x046c, 0x1e60, 0xa73c, 0x0170, 0x0472, 0x2160, 0x0370, 0x1e7e, 0x2161, 0x2c67, 0xa722,
0x0172, 0x0470, 0x2162, 0x2c64, 0x0372, 0x1e7c, 0x2163, 0x0174, 0x0476, 0x2164, 0x2c62, 0x1e7a, 0x2165, 0x2c63, 0xa726, 0x0176,
0x0474, 0x2166, 0x2c60, 0x0376, 0x1e78, 0x2167, 0xa724, 0x0178, 0x047a, 0x2168, 0x2c6e, 0x0179, 0x1e76, 0x2169, 0x2c6f, 0xa72a,
0x0478, 0x216a, 0x017b, 0x1e74, 0x216b, 0x2c6d, 0xa728, 0x047e, 0x216c, 0x017d, 0x1e72, 0x216d, 0x2c6b, 0xa72e, 0x037f, 0x047c,
0x216e, 0x017f, 0x1e70, 0x216f, 0x2c69, 0xa72c, 0x1f8f, 0x2c96, 0x0181, 0x1e8e, 0x1f8e, 0x0182, 0x0480, 0x1f8d, 0x2c94, 0x1e8c,
0x1f8c, 0x0184, 0x1f8b, 0x2c92, 0x1e8a, 0x1f8a, 0x0186, 0x1c88, 0x1f89, 0x2c90, 0x0187, 0x0386, 0x1e88, 0x1f88, 0x0389, 0x048a,
0x1c86, 0x2c9e, 0x0189, 0x0388, 0x1c87, 0x1e86, 0x018a, 0x1c84, 0x2c9c, 0x018b, 0x038a, 0x1c85, 0x1e84, 0x048e, 0x1c82, 0x2c9a,
0x038c, 0x1c83, 0x1e82, 0x018e, 0x038f, 0x048c, 0x1c80, 0x2c98, 0x018f, 0x038e, 0x1c81, 0x1e80, 0x0190, 0x0391, 0x0492, 0x1c9e,
0x1f9f, 0x2c86, 0x0191, 0x1c9f, 0x1e9e, 0x1f9e, 0xa7c2, 0x0393, 0x0490, 0x1c9c, 0x1f9d, 0x2c84, 0x0193, 0x0392, 0x1c9d, 0x1f9c,
0x2183, 0x0194, 0x0395, 0x0496, 0x1c9a, 0x1e9b, 0x1f9b, 0x2c82, 0xa7c7, 0x0394, 0x1c9b, 0x1f9a, 0xa7c6, 0x0196, 0x0397, 0x0494,
0x1c98, 0x1f99, 0x2c80, 0xa7c5, 0x0197, 0x0396, 0x1c99, 0x1f98, 0xa7c4, 0x0198, 0x0399, 0x049a, 0x1c96, 0x2c8e, 0x0398, 0x1c97,
0x039b, 0x0498, 0x1c94, 0x2c8c, 0xa7c9, 0x039a, 0x1c95, 0x1e94, 0x019c, 0x039d, 0x049e, 0x1c92, 0x2c8a, 0x019d, 0x039c, 0x1c93,
0x1e92, 0x039f, 0x049c, 0x1c90, 0x2c88, 0x019f, 0x039e, 0x1c91, 0x1e90, 0x01a0, 0x03a1, 0x04a2, 0x10a8, 0x1cae, 0x1faf, 0x2cb6,
0x03a0, 0x10a9, 0x1caf, 0x1eae, 0x1fae, 0x01a2, 0x03a3, 0x04a0, 0x10aa, 0x1cac, 0x1fad, 0x2cb4, 0x10ab, 0x1cad, 0x1eac, 0x1fac,
0x01a4, 0x03a5, 0x04a6, 0x10ac, 0x1caa, 0x1fab, 0x24b6, 0x2cb2, 0x03a4, 0x10ad, 0x1cab, 0x1eaa, 0x1faa, 0x24b7, 0x01a6, 0x03a7,
0x04a4, 0x10ae, 0x1ca8, 0x1fa9, 0x2cb0, 0xa7f5, 0x01a7, 0x03a6, 0x10af, 0x1ca9, 0x1ea8, 0x1fa8, 0x03a9, 0x04aa, 0x10a0, 0x1ca6,
0x24ba, 0x2cbe, 0x01a9, 0x03a8, 0x10a1, 0x1ca7, 0x1ea6, 0x24bb, 0x03ab, 0x04a8, 0x10a2, 0x1ca4, 0x24b8, 0x2cbc, 0x03aa, 0x10a3,
0x1ca5, 0x1ea4, 0x24b9, 0x01ac, 0x04ae, 0x10a4, 0x1ca2, 0x24be, 0x2cba, 0x10a5, 0x1ca3, 0x1ea2, 0x24bf, 0x01ae, 0x04ac, 0x10a6,
0x1ca0, 0x24bc, 0x2cb8, 0x01af, 0x10a7, 0x1ca1, 0x1ea0, 0x24bd, 0x04b2, 0x10b8, 0x1cbe, 0x2ca6, 0x01b1, 0x10b9, 0x1cbf, 0x1ebe,
0x1fbe, 0x01b2, 0x04b0, 0x10ba, 0x2ca4, 0x01b3, 0x10bb, 0x1cbd, 0x1ebc, 0x1fbc, 0x04b6, 0x10bc, 0x1cba, 0x1fbb, 0x2ca2, 0x00b5,
0x01b5, 0x10bd, 0x1eba, 0x1fba, 0x04b4, 0x10be, 0x1cb8, 0x1fb9, 0x2ca0, 0x01b7, 0x10bf, 0x1cb9, 0x1eb8, 0x1fb8, 0x01b8, 0x04ba,
0x10b0, 0x1cb6, 0x2cae, 0x10b1, 0x1cb7, 0x1eb6, 0x04b8, 0x10b2, 0x1cb4, 0x2cac, 0x10b3, 0x1cb5, 0x1eb4, 0x01bc, 0x04be, 0x10b4,
0x1cb2, 0x2caa, 0x10b5, 0x1cb3, 0x1eb2, 0x04bc, 0x10b6, 0x1cb0, 0x2ca8, 0x10b7, 0x1cb1, 0x1eb0, 0x00c0, 0x2cd6, 0xab95, 0x00c1,
0x04c3, 0x1ece, 0xa692, 0xa792, 0xab94, 0x00c2, 0x04c0, 0x2cd4, 0xab97, 0x00c3, 0x03c2, 0x04c1, 0x1ecc, 0x1fcc, 0xa690, 0xa790,
0xab96, 0x00c4, 0x01c4, 0x1fcb, 0x2cd2, 0xab91, 0x00c5, 0x01c5, 0x04c7, 0x10cd, 0x1eca, 0x1fca, 0xa696, 0xa796, 0xab90, 0x00c6,
0x1fc9, 0x2cd0, 0xab93, 0x00c7, 0x01c7, 0x04c5, 0x1ec8, 0x1fc8, 0xa694, 0xab92, 0x00c8, 0x01c8, 0x10c0, 0x2cde, 0xab9d, 0x00c9,
0x04cb, 0x10c1, 0x1ec6, 0xa69a, 0xa79a, 0xab9c, 0x00ca, 0x01ca, 0x10c2, 0x2cdc, 0xab9f, 0x00cb, 0x01cb, 0x04c9, 0x10c3, 0x1ec4,
0xa698, 0xa798, 0xab9e, 0x00cc, 0x10c4, 0x2cda, 0xab99, 0x00cd, 0x01cd, 0x10c5, 0x1ec2, 0xa79e, 0xab98, 0x00ce, 0x03cf, 0x2cd8,
0xab9b, 0x00cf, 0x01cf, 0x04cd, 0x10c7, 0x1ec0, 0xa79c, 0xab9a, 0x00d0, 0x03d1, 0x04d2, 0x24c2, 0x2cc6, 0xab85, 0x00d1, 0x01d1,
0x03d0, 0x1ede, 0x24c3, 0xa682, 0xa782, 0xab84, 0x00d2, 0x04d0, 0x24c0, 0x2cc4, 0xab87, 0x00d3, 0x01d3, 0x1edc, 0x24c1, 0xa680,
0xa780, 0xab86, 0x00d4, 0x03d5, 0x04d6, 0x1fdb, 0x24c6, 0x2cc2, 0xab81, 0x00d5, 0x01d5, 0x1eda, 0x1fda, 0x24c7, 0xa686, 0xa786,
0xab80, 0x00d6, 0x04d4, 0x1fd9, 0x24c4, 0x2cc0, 0xab83, 0x01d7, 0x03d6, 0x1ed8, 0x1fd8, 0x24c5, 0xa684, 0xa784, 0xab82, 0x00d8,
0x04da, 0x24ca, 0x2cce, 0xa78b, 0xab8d, 0x00d9, 0x01d9, 0x03d8, 0x1ed6, 0x24cb, 0xa68a, 0xab8c, 0x00da, 0x04d8, 0x24c8, 0x2ccc,
0xab8f, 0x00db, 0x01db, 0x03da, 0x1ed4, 0x24c9, 0xa688, 0xab8e, 0x00dc, 0x04de, 0x24ce, 0x2cca, 0xab89, 0x00dd, 0x03dc, 0x1ed2,
0x24cf, 0xa68e, 0xab88, 0x00de, 0x01de, 0x04dc, 0x24cc, 0x2cc8, 0xa78d, 0xab8b, 0x03de, 0x1ed0, 0x24cd, 0xa68c, 0xab8a, 0x01e0,
0x04e2, 0xa7b3, 0xabb5, 0x03e0, 0x1eee, 0xa7b2, 0xabb4, 0x01e2, 0x04e0, 0xa7b1, 0xabb7, 0x03e2, 0x1eec, 0x1fec, 0xa7b0, 0xabb6,
0x01e4, 0x04e6, 0x1feb, 0x2cf2, 0xabb1, 0x03e4, 0x1eea, 0x1fea, 0xa7b6, 0xabb0, 0x01e6, 0x04e4, 0x1fe9, 0xabb3, 0x03e6, 0x1ee8,
0x1fe8, 0xa7b4, 0xabb2, 0x01e8, 0x04ea, 0xabbd, 0x03e8, 0x1ee6, 0xa7ba, 0xabbc, 0x01ea, 0x04e8, 0xabbf, 0x03ea, 0x1ee4, 0xa7b8,
0xabbe, 0x01ec, 0x04ee, 0xabb9, 0x03ec, 0x1ee2, 0xa7be, 0xabb8, 0x01ee, 0x04ec, 0xabbb, 0x03ee, 0x1ee0, 0xa7bc, 0xabba, 0x03f1,
0x04f2, 0x13f9, 0xaba5, 0x01f1, 0x03f0, 0x13f8, 0x1efe, 0xa7a2, 0xaba4, 0x01f2, 0x04f0, 0x13fb, 0xaba7, 0x13fa, 0x1efc, 0x1ffc,
0xa7a0, 0xaba6, 0x01f4, 0x03f5, 0x04f6, 0x13fd, 0x1ffb, 0x2ce2, 0xaba1, 0x03f4, 0x13fc, 0x1efa, 0x1ffa, 0xa7a6, 0xaba0, 0x01f6,
0x03f7, 0x04f4, 0x1ff9, 0x2ce0, 0xaba3, 0x01f7, 0x1ef8, 0x1ff8, 0xa7a4, 0xaba2, 0x01f8, 0x03f9, 0x04fa, 0xa7ab, 0xabad, 0x1ef6,
0xa7aa, 0xabac, 0x01fa, 0x04f8, 0xabaf, 0x03fa, 0x1ef4, 0x2ced, 0xa7a8, 0xabae, 0x01fc, 0x03fd, 0x04fe, 0xaba9, 0x1ef2, 0x2ceb,
0xa7ae, 0xaba8, 0x01fe, 0x03ff, 0x04fc, 0xa7ad, 0xabab, 0x03fe, 0x1ef0, 0xa7ac, 0xabaa
};
static const unsigned short casefold16_1_vals[1163] = {
0x0101, 0x0452, 0x0503, 0x1f07, 0x2c46, 0x0201, 0x0453, 0x1e0f, 0x1f06, 0x2c47, 0xa653, 0xa753, 0x0103, 0x0450, 0x0501, 0x1f05,
0x2c44, 0x0203, 0x0451, 0x1e0d, 0x1f04, 0x2c45, 0xa651, 0xa751, 0x0105, 0x0456, 0x0507, 0x1f03, 0x2c42, 0x0205, 0x0457, 0x1e0b,
0x1f02, 0x2c43, 0xa657, 0xa757, 0x0107, 0x0454, 0x0505, 0x1f01, 0x2c40, 0x0207, 0x0455, 0x1e09, 0x1f00, 0x2c41, 0xa655, 0xa755,
0x0109, 0x045a, 0x050b, 0x2c4e, 0x0209, 0x045b, 0x1e07, 0x2c4f, 0xa65b, 0xa75b, 0x010b, 0x0458, 0x0509, 0x2c4c, 0x020b, 0x0459,
0x1e05, 0x2c4d, 0xa659, 0xa759, 0x010d, 0x045e, 0x050f, 0x2c4a, 0x020d, 0x045f, 0x1e03, 0x2c4b, 0xa65f, 0xa75f, 0x010f, 0x045c,
0x050d, 0x2c48, 0x020f, 0x045d, 0x1e01, 0x2c49, 0xa65d, 0xa75d, 0x0111, 0x0432, 0x0513, 0x2c36, 0x0211, 0x0433, 0x1e1f, 0x2c37,
0xa643, 0xa743, 0x0113, 0x0430, 0x0511, 0x1f15, 0x2c34, 0x0213, 0x0431, 0x1e1d, 0x1f14, 0x2c35, 0xa641, 0xa741, 0x0115, 0x0436,
0x0517, 0x1f13, 0x2c32, 0x0215, 0x0437, 0x1e1b, 0x1f12, 0x2c33, 0xa647, 0xa747, 0x0117, 0x0434, 0x0515, 0x1f11, 0x2c30, 0x0217,
0x0435, 0x1e19, 0x1f10, 0x2c31, 0xa645, 0xa745, 0x0119, 0x043a, 0x051b, 0x2c3e, 0x0219, 0x043b, 0x1e17, 0x2c3f, 0xa64b, 0xa74b,
0x011b, 0x0438, 0x0519, 0x2c3c, 0x021b, 0x0439, 0x1e15, 0x2c3d, 0xa649, 0xa749, 0x011d, 0x043e, 0x051f, 0x2c3a, 0x021d, 0x043f,
0x1e13, 0x2c3b, 0xa64f, 0xa74f, 0x011f, 0x043c, 0x051d, 0x2c38, 0x021f, 0x043d, 0x1e11, 0x2c39, 0xa64d, 0xa74d, 0x0121, 0x0442,
0x0523, 0x1f27, 0x13a5, 0x019e, 0x0443, 0x1e2f, 0x1f26, 0x13a4, 0x0123, 0x0440, 0x0521, 0x1f25, 0x214e, 0x13a7, 0x0223, 0x0441,
0x1e2d, 0x1f24, 0x13a6, 0x0125, 0x0446, 0x0527, 0x1f23, 0x13a1, 0x0225, 0x0447, 0x1e2b, 0x1f22, 0x13a0, 0x0127, 0x0444, 0x0525,
0x1f21, 0x13a3, 0x0227, 0x0445, 0x1e29, 0x1f20, 0x13a2, 0x0129, 0x044a, 0x052b, 0xa77c, 0x13ad, 0x0229, 0x044b, 0x1e27, 0x13ac,
0x012b, 0x0448, 0x0529, 0xa77a, 0x13af, 0x022b, 0x0449, 0x1e25, 0x13ae, 0x012d, 0x044e, 0x052f, 0x13a9, 0x022d, 0x044f, 0x1e23,
0xa77f, 0x13a8, 0x012f, 0x044c, 0x052d, 0x1d79, 0x13ab, 0x022f, 0x044d, 0x1e21, 0x13aa, 0x0562, 0x1f37, 0x2c56, 0x0231, 0x0563,
0x1e3f, 0x1f36, 0x2c57, 0xa663, 0xa763, 0x0133, 0x1f35, 0x2c54, 0x0233, 0x0561, 0x1e3d, 0x1f34, 0x2c55, 0xa661, 0xa761, 0x0135,
0x0566, 0x1f33, 0x2c52, 0x0567, 0x1e3b, 0x1f32, 0x2c53, 0xa667, 0xa767, 0x0137, 0x0564, 0x1f31, 0x03c9, 0x2c50, 0x0565, 0x1e39,
0x1f30, 0x2c51, 0xa665, 0xa765, 0x056a, 0x2c5e, 0x013a, 0x056b, 0x1e37, 0xa66b, 0xa76b, 0x023c, 0x0568, 0x006b, 0x2c5c, 0x013c,
0x2c65, 0x0569, 0x1e35, 0x00e5, 0x2c5d, 0xa669, 0xa769, 0x019a, 0x056e, 0x2c5a, 0x013e, 0x056f, 0x1e33, 0x2c5b, 0xa76f, 0x056c,
0x2c58, 0x0140, 0x2c66, 0x056d, 0x1e31, 0x2c59, 0xa66d, 0xa76d, 0x0242, 0x0572, 0x0142, 0x0573, 0x1e4f, 0x0180, 0x0570, 0x1f45,
0x0144, 0x0571, 0x1e4d, 0x1f44, 0x028c, 0x03b9, 0x0576, 0x1f43, 0x0146, 0x0289, 0x0577, 0x1e4b, 0x1f42, 0xff5a, 0x0574, 0x1f41,
0xff59, 0x0148, 0x0247, 0x0575, 0x1e49, 0x1f40, 0xff58, 0x057a, 0xff57, 0x0249, 0x057b, 0x1e47, 0xff56, 0x014b, 0x0578, 0xff55,
0x024b, 0x0579, 0x1e45, 0xff54, 0x014d, 0x057e, 0xff53, 0x024d, 0x057f, 0x1e43, 0xff52, 0x014f, 0x057c, 0xff51, 0x024f, 0x057d,
0x1e41, 0xff50, 0x0151, 0x0582, 0x1f57, 0xff4f, 0x0583, 0x1e5f, 0xff4e, 0x0153, 0x0580, 0x1f55, 0xff4d, 0x0581, 0x1e5d, 0xff4c,
0x0155, 0x0586, 0x1f53, 0xff4b, 0x1e5b, 0xff4a, 0x0157, 0x0584, 0x1f51, 0xff49, 0x0585, 0x1e59, 0xff48, 0x0159, 0xff47, 0x1e57,
0xff46, 0x015b, 0xff45, 0x1e55, 0xff44, 0x015d, 0xff43, 0x1e53, 0xff42, 0x015f, 0xff41, 0x1e51, 0x0161, 0x0463, 0x1f67, 0x1e6f,
0x1f66, 0xa733, 0x0163, 0x0461, 0x1f65, 0x1e6d, 0x1f64, 0x2c76, 0x0165, 0x0467, 0x1f63, 0x2c73, 0x1e6b, 0x1f62, 0xa737, 0x0167,
0x0465, 0x1f61, 0x0252, 0x1e69, 0x1f60, 0xa735, 0x0169, 0x046b, 0x023f, 0x1e67, 0x0240, 0xa73b, 0x016b, 0x0469, 0x1e65, 0xa739,
0x016d, 0x046f, 0x1e63, 0xa73f, 0x016f, 0x046d, 0x1e61, 0xa73d, 0x0171, 0x0473, 0x2170, 0x0371, 0x1e7f, 0x2171, 0x2c68, 0xa723,
0x0173, 0x0471, 0x2172, 0x027d, 0x0373, 0x1e7d, 0x2173, 0x0175, 0x0477, 0x2174, 0x026b, 0x1e7b, 0x2175, 0x1d7d, 0xa727, 0x0177,
0x0475, 0x2176, 0x2c61, 0x0377, 0x1e79, 0x2177, 0xa725, 0x00ff, 0x047b, 0x2178, 0x0271, 0x017a, 0x1e77, 0x2179, 0x0250, 0xa72b,
0x0479, 0x217a, 0x017c, 0x1e75, 0x217b, 0x0251, 0xa729, 0x047f, 0x217c, 0x017e, 0x1e73, 0x217d, 0x2c6c, 0xa72f, 0x03f3, 0x047d,
0x217e, 0x0073, 0x1e71, 0x217f, 0x2c6a, 0xa72d, 0x1f87, 0x2c97, 0x0253, 0x1e8f, 0x1f86, 0x0183, 0x0481, 0x1f85, 0x2c95, 0x1e8d,
0x1f84, 0x0185, 0x1f83, 0x2c93, 0x1e8b, 0x1f82, 0x0254, 0xa64b, 0x1f81, 0x2c91, 0x0188, 0x03ac, 0x1e89, 0x1f80, 0x03ae, 0x048b,
0x044a, 0x2c9f, 0x0256, 0x03ad, 0x0463, 0x1e87, 0x0257, 0x0442, 0x2c9d, 0x018c, 0x03af, 0x0442, 0x1e85, 0x048f, 0x043e, 0x2c9b,
0x03cc, 0x0441, 0x1e83, 0x01dd, 0x03ce, 0x048d, 0x0432, 0x2c99, 0x0259, 0x03cd, 0x0434, 0x1e81, 0x025b, 0x03b1, 0x0493, 0x10de,
0x1f97, 0x2c87, 0x0192, 0x10df, 0x00df, 0x1f96, 0xa7c3, 0x03b3, 0x0491, 0x10dc, 0x1f95, 0x2c85, 0x0260, 0x03b2, 0x10dd, 0x1f94,
0x2184, 0x0263, 0x03b5, 0x0497, 0x10da, 0x1e61, 0x1f93, 0x2c83, 0xa7c8, 0x03b4, 0x10db, 0x1f92, 0x1d8e, 0x0269, 0x03b7, 0x0495,
0x10d8, 0x1f91, 0x2c81, 0x0282, 0x0268, 0x03b6, 0x10d9, 0x1f90, 0xa794, 0x0199, 0x03b9, 0x049b, 0x10d6, 0x2c8f, 0x03b8, 0x10d7,
0x03bb, 0x0499, 0x10d4, 0x2c8d, 0xa7ca, 0x03ba, 0x10d5, 0x1e95, 0x026f, 0x03bd, 0x049f, 0x10d2, 0x2c8b, 0x0272, 0x03bc, 0x10d3,
0x1e93, 0x03bf, 0x049d, 0x10d0, 0x2c89, 0x0275, 0x03be, 0x10d1, 0x1e91, 0x01a1, 0x03c1, 0x04a3, 0x2d08, 0x10ee, 0x1fa7, 0x2cb7,
0x03c0, 0x2d09, 0x10ef, 0x1eaf, 0x1fa6, 0x01a3, 0x03c3, 0x04a1, 0x2d0a, 0x10ec, 0x1fa5, 0x2cb5, 0x2d0b, 0x10ed, 0x1ead, 0x1fa4,
0x01a5, 0x03c5, 0x04a7, 0x2d0c, 0x10ea, 0x1fa3, 0x24d0, 0x2cb3, 0x03c4, 0x2d0d, 0x10eb, 0x1eab, 0x1fa2, 0x24d1, 0x0280, 0x03c7,
0x04a5, 0x2d0e, 0x10e8, 0x1fa1, 0x2cb1, 0xa7f6, 0x01a8, 0x03c6, 0x2d0f, 0x10e9, 0x1ea9, 0x1fa0, 0x03c9, 0x04ab, 0x2d00, 0x10e6,
0x24d4, 0x2cbf, 0x0283, 0x03c8, 0x2d01, 0x10e7, 0x1ea7, 0x24d5, 0x03cb, 0x04a9, 0x2d02, 0x10e4, 0x24d2, 0x2cbd, 0x03ca, 0x2d03,
0x10e5, 0x1ea5, 0x24d3, 0x01ad, 0x04af, 0x2d04, 0x10e2, 0x24d8, 0x2cbb, 0x2d05, 0x10e3, 0x1ea3, 0x24d9, 0x0288, 0x04ad, 0x2d06,
0x10e0, 0x24d6, 0x2cb9, 0x01b0, 0x2d07, 0x10e1, 0x1ea1, 0x24d7, 0x04b3, 0x2d18, 0x10fe, 0x2ca7, 0x028a, 0x2d19, 0x10ff, 0x1ebf,
0x03b9, 0x028b, 0x04b1, 0x2d1a, 0x2ca5, 0x01b4, 0x2d1b, 0x10fd, 0x1ebd, 0x1fb3, 0x04b7, 0x2d1c, 0x10fa, 0x1f71, 0x2ca3, 0x03bc,
0x01b6, 0x2d1d, 0x1ebb, 0x1f70, 0x04b5, 0x2d1e, 0x10f8, 0x1fb1, 0x2ca1, 0x0292, 0x2d1f, 0x10f9, 0x1eb9, 0x1fb0, 0x01b9, 0x04bb,
0x2d10, 0x10f6, 0x2caf, 0x2d11, 0x10f7, 0x1eb7, 0x04b9, 0x2d12, 0x10f4, 0x2cad, 0x2d13, 0x10f5, 0x1eb5, 0x01bd, 0x04bf, 0x2d14,
0x10f2, 0x2cab, 0x2d15, 0x10f3, 0x1eb3, 0x04bd, 0x2d16, 0x10f0, 0x2ca9, 0x2d17, 0x10f1, 0x1eb1, 0x00e0, 0x2cd7, 0x13c5, 0x00e1,
0x04c4, 0x1ecf, 0xa693, 0xa793, 0x13c4, 0x00e2, 0x04cf, 0x2cd5, 0x13c7, 0x00e3, 0x03c3, 0x04c2, 0x1ecd, 0x1fc3, 0xa691, 0xa791,
0x13c6, 0x00e4, 0x01c6, 0x1f75, 0x2cd3, 0x13c1, 0x00e5, 0x01c6, 0x04c8, 0x2d2d, 0x1ecb, 0x1f74, 0xa697, 0xa797, 0x13c0, 0x00e6,
0x1f73, 0x2cd1, 0x13c3, 0x00e7, 0x01c9, 0x04c6, 0x1ec9, 0x1f72, 0xa695, 0x13c2, 0x00e8, 0x01c9, 0x2d20, 0x2cdf, 0x13cd, 0x00e9,
0x04cc, 0x2d21, 0x1ec7, 0xa69b, 0xa79b, 0x13cc, 0x00ea, 0x01cc, 0x2d22, 0x2cdd, 0x13cf, 0x00eb, 0x01cc, 0x04ca, 0x2d23, 0x1ec5,
0xa699, 0xa799, 0x13ce, 0x00ec, 0x2d24, 0x2cdb, 0x13c9, 0x00ed, 0x01ce, 0x2d25, 0x1ec3, 0xa79f, 0x13c8, 0x00ee, 0x03d7, 0x2cd9,
0x13cb, 0x00ef, 0x01d0, 0x04ce, 0x2d27, 0x1ec1, 0xa79d, 0x13ca, 0x00f0, 0x03b8, 0x04d3, 0x24dc, 0x2cc7, 0x13b5, 0x00f1, 0x01d2,
0x03b2, 0x1edf, 0x24dd, 0xa683, 0xa783, 0x13b4, 0x00f2, 0x04d1, 0x24da, 0x2cc5, 0x13b7, 0x00f3, 0x01d4, 0x1edd, 0x24db, 0xa681,
0xa781, 0x13b6, 0x00f4, 0x03c6, 0x04d7, 0x1f77, 0x24e0, 0x2cc3, 0x13b1, 0x00f5, 0x01d6, 0x1edb, 0x1f76, 0x24e1, 0xa687, 0xa787,
0x13b0, 0x00f6, 0x04d5, 0x1fd1, 0x24de, 0x2cc1, 0x13b3, 0x01d8, 0x03c0, 0x1ed9, 0x1fd0, 0x24df, 0xa685, 0xa785, 0x13b2, 0x00f8,
0x04db, 0x24e4, 0x2ccf, 0xa78c, 0x13bd, 0x00f9, 0x01da, 0x03d9, 0x1ed7, 0x24e5, 0xa68b, 0x13bc, 0x00fa, 0x04d9, 0x24e2, 0x2ccd,
0x13bf, 0x00fb, 0x01dc, 0x03db, 0x1ed5, 0x24e3, 0xa689, 0x13be, 0x00fc, 0x04df, 0x24e8, 0x2ccb, 0x13b9, 0x00fd, 0x03dd, 0x1ed3,
0x24e9, 0xa68f, 0x13b8, 0x00fe, 0x01df, 0x04dd, 0x24e6, 0x2cc9, 0x0265, 0x13bb, 0x03df, 0x1ed1, 0x24e7, 0xa68d, 0x13ba, 0x01e1,
0x04e3, 0xab53, 0x13e5, 0x03e1, 0x1eef, 0x029d, 0x13e4, 0x01e3, 0x04e1, 0x0287, 0x13e7, 0x03e3, 0x1eed, 0x1fe5, 0x029e, 0x13e6,
0x01e5, 0x04e7, 0x1f7b, 0x2cf3, 0x13e1, 0x03e5, 0x1eeb, 0x1f7a, 0xa7b7, 0x13e0, 0x01e7, 0x04e5, 0x1fe1, 0x13e3, 0x03e7, 0x1ee9,
0x1fe0, 0xa7b5, 0x13e2, 0x01e9, 0x04eb, 0x13ed, 0x03e9, 0x1ee7, 0xa7bb, 0x13ec, 0x01eb, 0x04e9, 0x13ef, 0x03eb, 0x1ee5, 0xa7b9,
0x13ee, 0x01ed, 0x04ef, 0x13e9, 0x03ed, 0x1ee3, 0xa7bf, 0x13e8, 0x01ef, 0x04ed, 0x13eb, 0x03ef, 0x1ee1, 0xa7bd, 0x13ea, 0x03c1,
0x04f3, 0x13f1, 0x13d5, 0x01f3, 0x03ba, 0x13f0, 0x1eff, 0xa7a3, 0x13d4, 0x01f3, 0x04f1, 0x13f3, 0x13d7, 0x13f2, 0x1efd, 0x1ff3,
0xa7a1, 0x13d6, 0x01f5, 0x03b5, 0x04f7, 0x13f5, 0x1f7d, 0x2ce3, 0x13d1, 0x03b8, 0x13f4, 0x1efb, 0x1f7c, 0xa7a7, 0x13d0, 0x0195,
0x03f8, 0x04f5, 0x1f79, 0x2ce1, 0x13d3, 0x01bf, 0x1ef9, 0x1f78, 0xa7a5, 0x13d2, 0x01f9, 0x03f2, 0x04fb, 0x025c, 0x13dd, 0x1ef7,
0x0266, 0x13dc, 0x01fb, 0x04f9, 0x13df, 0x03fb, 0x1ef5, 0x2cee, 0xa7a9, 0x13de, 0x01fd, 0x037b, 0x04ff, 0x13d9, 0x1ef3, 0x2cec,
0x026a, 0x13d8, 0x01ff, 0x037d, 0x04fd, 0x026c, 0x13db, 0x037c, 0x1ef1, 0x0261, 0x13da
};
static const unsigned short perbucket16_1_accu[257] = {
0, 5, 12, 17, 24, 29, 36, 41, 48, 52, 58, 62, 68, 72, 78, 82, 88, 92, 98, 103, 110, 115, 122, 127, 134, 138, 144, 148, 154, 158, 164, 168,
174, 179, 184, 190, 195, 200, 205, 210, 215, 220, 224, 229, 233, 237, 242, 247, 251, 254, 261, 264, 271, 275, 281, 286, 292, 294, 299, 303, 311, 314, 319, 321,
328, 330, 333, 336, 340, 344, 350, 353, 359, 361, 365, 368, 372, 375, 379, 382, 386, 390, 393, 397, 400, 404, 406, 410, 413, 415, 417, 419, 421, 423, 425, 427,
428, 431, 434, 437, 440, 444, 447, 451, 454, 457, 460, 462, 464, 466, 468, 470, 472, 475, 480, 484, 487, 491, 495, 499, 503, 507, 512, 514, 519, 521, 526, 529,
534, 536, 539, 543, 545, 548, 550, 554, 558, 562, 566, 569, 573, 576, 579, 584, 588, 594, 599, 604, 609, 617, 621, 628, 633, 638, 640, 645, 648, 653, 657, 661,
665, 672, 677, 684, 688, 696, 702, 710, 716, 722, 728, 734, 739, 745, 749, 755, 760, 764, 769, 773, 778, 783, 788, 793, 798, 803, 806, 810, 813, 818, 821, 825,
828, 831, 837, 841, 849, 854, 863, 867, 874, 879, 886, 891, 899, 903, 909, 913, 920, 926, 934, 939, 946, 953, 961, 967, 975, 981, 988, 993, 1000, 1005, 1011, 1018,
1023, 1027, 1031, 1035, 1040, 1045, 1050, 1054, 1059, 1062, 1066, 1069, 1073, 1076, 1080, 1083, 1087, 1091, 1097, 1101, 1106, 1113, 1119, 1125, 1130, 1135, 1138, 1141, 1146, 1150, 1154, 1159,
1163
};
static const unsigned short casefold32_1_keys[225] = {
0x0c86, 0x0c87, 0x0c84, 0x0c85, 0x0c82, 0x0c83, 0x0c80, 0x0c81, 0x0c8e, 0x0c8f, 0x0c8c, 0x0c8d, 0x0c8a, 0x0c8b, 0x0c88, 0x0c89,
0x0c96, 0x0c97, 0x0c94, 0x0c95, 0x0c92, 0x0c93, 0x0c90, 0x0c91, 0x0c9e, 0x0c9f, 0x0c9c, 0x0c9d, 0x0c9a, 0x0c9b, 0x0c98, 0x0c99,
0x0ca6, 0x18ac, 0x0ca7, 0x18ad, 0x0ca4, 0x18ae, 0x0ca5, 0x18af, 0x0ca2, 0x18a8, 0x0ca3, 0x18a9, 0x0ca0, 0x18aa, 0x0ca1, 0x18ab,
0x0cae, 0x18a4, 0x0caf, 0x18a5, 0x0cac, 0x18a6, 0x0cad, 0x18a7, 0x0caa, 0x18a0, 0x0cab, 0x18a1, 0x0ca8, 0x18a2, 0x0ca9, 0x18a3,
0x04b2, 0x18bc, 0x04b3, 0x18bd, 0x04b0, 0x18be, 0x04b1, 0x18bf, 0x04b6, 0x0cb2, 0x18b8, 0x04b7, 0x18b9, 0x04b4, 0x0cb0, 0x18ba,
0x04b5, 0x0cb1, 0x18bb, 0x04ba, 0x18b4, 0x04bb, 0x18b5, 0x04b8, 0x18b6, 0x04b9, 0x18b7, 0x04be, 0x18b0, 0x04bf, 0x18b1, 0x04bc,
0x18b2, 0x04bd, 0x18b3, 0x04c2, 0x04c3, 0x04c0, 0x04c1, 0x04c6, 0x04c7, 0x04c4, 0x04c5, 0x04ca, 0x04cb, 0x04c8, 0x04c9, 0x04ce,
0x04cf, 0x04cc, 0x04cd, 0x04d2, 0x04d3, 0x04d0, 0x04d1, 0x0402, 0x0403, 0x0400, 0x0401, 0x0406, 0x0407, 0x0404, 0x0405, 0x040a,
0x040b, 0x0408, 0x0409, 0x040e, 0x040f, 0x040c, 0x040d, 0x0412, 0x0413, 0x0410, 0x0411, 0x0416, 0x0417, 0x0414, 0x0415, 0x041a,
0x041b, 0x0418, 0x0419, 0x041e, 0x041f, 0x041c, 0x041d, 0x0422, 0x0423, 0x0420, 0x0421, 0x0426, 0x0427, 0x0424, 0x0425, 0xe920,
0xe921, 0x6e57, 0xe914, 0x6e56, 0xe915, 0x6e55, 0xe916, 0x6e54, 0xe917, 0x6e53, 0xe910, 0x6e52, 0xe911, 0x6e51, 0xe912, 0x6e50,
0xe913, 0x6e5f, 0xe91c, 0x6e5e, 0xe91d, 0x6e5d, 0xe91e, 0x6e5c, 0xe91f, 0x6e5b, 0xe918, 0x6e5a, 0xe919, 0x6e59, 0xe91a, 0x6e58,
0xe91b, 0x6e47, 0xe904, 0x6e46, 0xe905, 0x6e45, 0xe906, 0x6e44, 0xe907, 0x6e43, 0xe900, 0x6e42, 0xe901, 0x6e41, 0xe902, 0x6e40,
0xe903, 0x6e4f, 0xe90c, 0x6e4e, 0xe90d, 0x6e4d, 0xe90e, 0x6e4c, 0xe90f, 0x6e4b, 0xe908, 0x6e4a, 0xe909, 0x6e49, 0xe90a, 0x6e48,
0xe90b
};
static const unsigned short casefold32_1_vals[225] = {
0x0cc6, 0x0cc7, 0x0cc4, 0x0cc5, 0x0cc2, 0x0cc3, 0x0cc0, 0x0cc1, 0x0cce, 0x0ccf, 0x0ccc, 0x0ccd, 0x0cca, 0x0ccb, 0x0cc8, 0x0cc9,
0x0cd6, 0x0cd7, 0x0cd4, 0x0cd5, 0x0cd2, 0x0cd3, 0x0cd0, 0x0cd1, 0x0cde, 0x0cdf, 0x0cdc, 0x0cdd, 0x0cda, 0x0cdb, 0x0cd8, 0x0cd9,
0x0ce6, 0x18cc, 0x0ce7, 0x18cd, 0x0ce4, 0x18ce, 0x0ce5, 0x18cf, 0x0ce2, 0x18c8, 0x0ce3, 0x18c9, 0x0ce0, 0x18ca, 0x0ce1, 0x18cb,
0x0cee, 0x18c4, 0x0cef, 0x18c5, 0x0cec, 0x18c6, 0x0ced, 0x18c7, 0x0cea, 0x18c0, 0x0ceb, 0x18c1, 0x0ce8, 0x18c2, 0x0ce9, 0x18c3,
0x04da, 0x18dc, 0x04db, 0x18dd, 0x04d8, 0x18de, 0x04d9, 0x18df, 0x04de, 0x0cf2, 0x18d8, 0x04df, 0x18d9, 0x04dc, 0x0cf0, 0x18da,
0x04dd, 0x0cf1, 0x18db, 0x04e2, 0x18d4, 0x04e3, 0x18d5, 0x04e0, 0x18d6, 0x04e1, 0x18d7, 0x04e6, 0x18d0, 0x04e7, 0x18d1, 0x04e4,
0x18d2, 0x04e5, 0x18d3, 0x04ea, 0x04eb, 0x04e8, 0x04e9, 0x04ee, 0x04ef, 0x04ec, 0x04ed, 0x04f2, 0x04f3, 0x04f0, 0x04f1, 0x04f6,
0x04f7, 0x04f4, 0x04f5, 0x04fa, 0x04fb, 0x04f8, 0x04f9, 0x042a, 0x042b, 0x0428, 0x0429, 0x042e, 0x042f, 0x042c, 0x042d, 0x0432,
0x0433, 0x0430, 0x0431, 0x0436, 0x0437, 0x0434, 0x0435, 0x043a, 0x043b, 0x0438, 0x0439, 0x043e, 0x043f, 0x043c, 0x043d, 0x0442,
0x0443, 0x0440, 0x0441, 0x0446, 0x0447, 0x0444, 0x0445, 0x044a, 0x044b, 0x0448, 0x0449, 0x044e, 0x044f, 0x044c, 0x044d, 0xe942,
0xe943, 0x6e77, 0xe936, 0x6e76, 0xe937, 0x6e75, 0xe938, 0x6e74, 0xe939, 0x6e73, 0xe932, 0x6e72, 0xe933, 0x6e71, 0xe934, 0x6e70,
0xe935, 0x6e7f, 0xe93e, 0x6e7e, 0xe93f, 0x6e7d, 0xe940, 0x6e7c, 0xe941, 0x6e7b, 0xe93a, 0x6e7a, 0xe93b, 0x6e79, 0xe93c, 0x6e78,
0xe93d, 0x6e67, 0xe926, 0x6e66, 0xe927, 0x6e65, 0xe928, 0x6e64, 0xe929, 0x6e63, 0xe922, 0x6e62, 0xe923, 0x6e61, 0xe924, 0x6e60,
0xe925, 0x6e6f, 0xe92e, 0x6e6e, 0xe92f, 0x6e6d, 0xe930, 0x6e6c, 0xe931, 0x6e6b, 0xe92a, 0x6e6a, 0xe92b, 0x6e69, 0xe92c, 0x6e68,
0xe92d
};
static const unsigned short perbucket32_1_accu[257] = {
0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 75, 77, 80, 83, 85, 87, 89, 91, 93, 95, 97,
99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119,
119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119,
119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150,
151, 152, 153, 154, 155, 156, 157, 158, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159,
159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 160, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161,
161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181, 183, 185, 187, 189, 191, 193, 195, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221, 223,
225
};
// Total size of referenced data: 6580 bytes
static CasefoldData casefoldData[] = {
{casefold16_1_keys, casefold16_1_vals, perbucket16_1_accu, 1, 0x0},
{casefold32_1_keys, casefold32_1_vals, perbucket32_1_accu, 1, 0x10000}
};