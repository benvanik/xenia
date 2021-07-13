test_vspltisw_1_GEN:
  vspltisw v1, 0
  blr
  #_ REGISTER_OUT v1 [00000000, 00000000, 00000000, 00000000]

test_vspltisw_2_GEN:
  vspltisw v1, 1
  blr
  #_ REGISTER_OUT v1 [00000001, 00000001, 00000001, 00000001]

test_vspltisw_3_GEN:
  vspltisw v1, 2
  blr
  #_ REGISTER_OUT v1 [00000002, 00000002, 00000002, 00000002]

test_vspltisw_4_GEN:
  vspltisw v1, 7
  blr
  #_ REGISTER_OUT v1 [00000007, 00000007, 00000007, 00000007]

