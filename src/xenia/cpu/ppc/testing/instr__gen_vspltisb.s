test_vspltisb_1_GEN:
  vspltisb v1, 0
  blr
  #_ REGISTER_OUT v1 [00000000, 00000000, 00000000, 00000000]

test_vspltisb_2_GEN:
  vspltisb v1, 1
  blr
  #_ REGISTER_OUT v1 [01010101, 01010101, 01010101, 01010101]

test_vspltisb_3_GEN:
  vspltisb v1, 2
  blr
  #_ REGISTER_OUT v1 [02020202, 02020202, 02020202, 02020202]

test_vspltisb_4_GEN:
  vspltisb v1, 7
  blr
  #_ REGISTER_OUT v1 [07070707, 07070707, 07070707, 07070707]

