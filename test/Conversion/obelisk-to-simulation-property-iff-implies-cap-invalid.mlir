// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s

// Nine two-literal DNF cubes make the false-LHS arm of `implies` require a
// raw 2^9 = 512-product De Morgan expansion. The raw admission cap applies
// before duplicate/subsumption normalization or optional Z3 minimization.
// CHECK: error: SVA property operator 'implies' currently requires nonvacuous one-cycle Boolean DNF operands without first_match or match items and an exact expansion of at most 256 alternatives

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 15 : i64, operator_kind = 10 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 20 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 21 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.binary attributes {node_id = 22 : i64, operator_kind = 1 : i32} {
                      obelisk.sv.assertion.binary attributes {node_id = 23 : i64, operator_kind = 1 : i32} {
                        obelisk.sv.assertion.binary attributes {node_id = 24 : i64, operator_kind = 1 : i32} {
                          obelisk.sv.assertion.binary attributes {node_id = 25 : i64, operator_kind = 1 : i32} {
                            obelisk.sv.assertion.binary attributes {node_id = 26 : i64, operator_kind = 1 : i32} {
                              obelisk.sv.assertion.binary attributes {node_id = 27 : i64, operator_kind = 1 : i32} {
                                obelisk.sv.assertion.binary attributes {node_id = 100 : i64, operator_kind = 0 : i32} {
                                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 101 : i64, repetition_is_unbounded = false} {
                                    obelisk.sv.expression.named_value attributes {node_id = 102 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                                    }
                                  }
                                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 103 : i64, repetition_is_unbounded = false} {
                                    obelisk.sv.expression.named_value attributes {node_id = 104 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                                    }
                                  }
                                }
                                obelisk.sv.assertion.binary attributes {node_id = 110 : i64, operator_kind = 0 : i32} {
                                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 111 : i64, repetition_is_unbounded = false} {
                                    obelisk.sv.expression.named_value attributes {node_id = 112 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                                    }
                                  }
                                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 113 : i64, repetition_is_unbounded = false} {
                                    obelisk.sv.expression.named_value attributes {node_id = 114 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                                    }
                                  }
                                }
                              }
                              obelisk.sv.assertion.binary attributes {node_id = 120 : i64, operator_kind = 0 : i32} {
                                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 121 : i64, repetition_is_unbounded = false} {
                                  obelisk.sv.expression.named_value attributes {node_id = 122 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                                  }
                                }
                                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 123 : i64, repetition_is_unbounded = false} {
                                  obelisk.sv.expression.named_value attributes {node_id = 124 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                                  }
                                }
                              }
                            }
                            obelisk.sv.assertion.binary attributes {node_id = 130 : i64, operator_kind = 0 : i32} {
                              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 131 : i64, repetition_is_unbounded = false} {
                                obelisk.sv.expression.named_value attributes {node_id = 132 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                                }
                              }
                              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 133 : i64, repetition_is_unbounded = false} {
                                obelisk.sv.expression.named_value attributes {node_id = 134 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                                }
                              }
                            }
                          }
                          obelisk.sv.assertion.binary attributes {node_id = 140 : i64, operator_kind = 0 : i32} {
                            obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 141 : i64, repetition_is_unbounded = false} {
                              obelisk.sv.expression.named_value attributes {node_id = 142 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              }
                            }
                            obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 143 : i64, repetition_is_unbounded = false} {
                              obelisk.sv.expression.named_value attributes {node_id = 144 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              }
                            }
                          }
                        }
                        obelisk.sv.assertion.binary attributes {node_id = 150 : i64, operator_kind = 0 : i32} {
                          obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 151 : i64, repetition_is_unbounded = false} {
                            obelisk.sv.expression.named_value attributes {node_id = 152 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            }
                          }
                          obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 153 : i64, repetition_is_unbounded = false} {
                            obelisk.sv.expression.named_value attributes {node_id = 154 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            }
                          }
                        }
                      }
                      obelisk.sv.assertion.binary attributes {node_id = 160 : i64, operator_kind = 0 : i32} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 161 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {node_id = 162 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 163 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {node_id = 164 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                    }
                    obelisk.sv.assertion.binary attributes {node_id = 170 : i64, operator_kind = 0 : i32} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 171 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 172 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 173 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 174 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 180 : i64, operator_kind = 0 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 181 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 182 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 183 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 184 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 200 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 201 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
