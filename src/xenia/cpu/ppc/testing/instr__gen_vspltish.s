test_vspltish_1_GEN:
  vspltish v1, 0
  blr
  #_ REGISTER_OUT v1 [00000000, 00000000, 00000000, 00000000]

test_vspltish_2_GEN:
  vspltish v1, 1
  blr
  #_ REGISTER_OUT v1 [00010001, 00010001, 00010001, 00010001]

test_vspltish_3_GEN:
  vspltish v1, 2
  blr
  #_ REGISTER_OUT v1 [00020002, 00020002, 00020002, 00020002]

test_vspltish_4_GEN:
  vspltish v1, 7
  blr
  #_ REGISTER_OUT v1 [00070007, 00070007, 00070007, 00070007]

