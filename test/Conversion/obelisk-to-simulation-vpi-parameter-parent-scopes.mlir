// RUN: obelisk-opt %s "--lower-obelisk-to-sim=vpi=read opt-level=0" -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ParameterAssignmentsInInstanceScopes
// RUN: obelisk-opt %s "--lower-obelisk-to-sim=vpi=full opt-level=3" -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ParameterAssignmentsInInstanceScopes
// RUN: obelisk-opt %t.opt --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.llvm
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!byte = !obelisk.integral<8, true, false, 7 : 0, byte>
!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @top attributes {node_id = 1 : i64, definition_kind = 0 : i32, hierarchical_name = "top", name = "top"} {
  }
  obelisk.sv.symbol.definition @ip attributes {node_id = 2 : i64, definition_kind = 1 : i32, hierarchical_name = "ip", name = "ip"} {
  }
  obelisk.sv.symbol.definition @child attributes {node_id = 3 : i64, definition_kind = 1 : i32, hierarchical_name = "child", name = "child"} {
  }
  obelisk.sv.symbol.definition @other attributes {node_id = 500 : i64, definition_kind = 0 : i32, hierarchical_name = "other", name = "other"} {
  }
  obelisk.sv.symbol.root @root attributes {node_id = 4 : i64, hierarchical_name = "$root", name = "$root"} {
    obelisk.sv.symbol.instance @top attributes {node_id = 5 : i64, hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top} {
      obelisk.sv.symbol.instance_body @top_body attributes {node_id = 6 : i64, hierarchical_name = "top", name = "top"} {
        obelisk.sv.symbol.instance @i attributes {node_id = 7 : i64, hierarchical_name = "top.i", is_uninstantiated = false, name = "i", referenced_path = "ip", referenced_symbol = @ip} {
          obelisk.sv.symbol.instance_body @i_body attributes {node_id = 8 : i64, hierarchical_name = "top.i", name = "ip"} {
            obelisk.sv.symbol.parameter @P attributes {node_id = 9 : i64, hierarchical_name = "top.i.P", name = "P", semantic_type = !int, constant_value = "11"} {
            }
            obelisk.sv.symbol.type_parameter @T attributes {node_id = 10 : i64, hierarchical_name = "top.i.T", name = "T", semantic_type = !byte} {
            }
            obelisk.sv.symbol.instance @a attributes {node_id = 11 : i64, hierarchical_name = "top.i.a", is_uninstantiated = false, name = "a", referenced_path = "child", referenced_symbol = @child} {
              obelisk.sv.symbol.instance_body @a_body attributes {node_id = 12 : i64, hierarchical_name = "top.i.a", name = "child"} {
                obelisk.sv.symbol.parameter @N attributes {node_id = 13 : i64, hierarchical_name = "top.i.a.N", name = "N", semantic_type = !int, constant_value = "11"} {
                }
                obelisk.sv.symbol.type_parameter @U attributes {node_id = 14 : i64, hierarchical_name = "top.i.a.U", name = "U", semantic_type = !byte} {
                }
              }
            }
            obelisk.sv.symbol.instance @b attributes {node_id = 15 : i64, hierarchical_name = "top.i.b", is_uninstantiated = false, name = "b", referenced_path = "child", referenced_symbol = @child} {
              obelisk.sv.symbol.instance_body @b_body attributes {node_id = 16 : i64, hierarchical_name = "top.i.b", name = "child"} {
                obelisk.sv.symbol.parameter @N attributes {node_id = 17 : i64, hierarchical_name = "top.i.b.N", name = "N", semantic_type = !int, constant_value = "7"} {
                }
                obelisk.sv.symbol.type_parameter @U attributes {node_id = 18 : i64, hierarchical_name = "top.i.b.U", name = "U", semantic_type = !byte} {
                }
              }
            }
            obelisk.sv.source.parameter_actual attributes {node_id = 19 : i64, parameter = @root::@top::@top_body::@i::@i_body::@a::@a_body::@N, ordinal = 0 : i64, is_named = unit} {
              obelisk.sv.source.parameter_reference attributes {node_id = 20 : i64, parameter = @P} {
              }
            }
            obelisk.sv.source.parameter_actual attributes {node_id = 21 : i64, parameter = @root::@top::@top_body::@i::@i_body::@b::@b_body::@N, ordinal = 1 : i64} {
              obelisk.sv.expression.integer_literal attributes {node_id = 22 : i64, constant_value = "7", is_signed = true, semantic_type = !int} {
              }
            }
            obelisk.sv.source.parameter_actual attributes {node_id = 23 : i64, parameter = @root::@top::@top_body::@i::@i_body::@a::@a_body::@U, ordinal = 2 : i64} {
              obelisk.sv.source.type_reference_default attributes {node_id = 24 : i64, parameter = @T} {
              }
            }
          }
        }
        obelisk.sv.symbol.instance @j attributes {node_id = 25 : i64, hierarchical_name = "top.j", is_uninstantiated = false, name = "j", referenced_path = "ip", referenced_symbol = @ip} {
          obelisk.sv.symbol.instance_body @j_body attributes {node_id = 26 : i64, hierarchical_name = "top.j", name = "ip"} {
            obelisk.sv.symbol.parameter @P attributes {node_id = 27 : i64, hierarchical_name = "top.j.P", name = "P", semantic_type = !int, constant_value = "13"} {
            }
            obelisk.sv.symbol.type_parameter @T attributes {node_id = 28 : i64, hierarchical_name = "top.j.T", name = "T", semantic_type = !bit} {
            }
            obelisk.sv.symbol.instance @a attributes {node_id = 29 : i64, hierarchical_name = "top.j.a", is_uninstantiated = false, name = "a", referenced_path = "child", referenced_symbol = @child} {
              obelisk.sv.symbol.instance_body @a_body attributes {node_id = 30 : i64, hierarchical_name = "top.j.a", name = "child"} {
                obelisk.sv.symbol.parameter @N attributes {node_id = 31 : i64, hierarchical_name = "top.j.a.N", name = "N", semantic_type = !int, constant_value = "13"} {
                }
                obelisk.sv.symbol.type_parameter @U attributes {node_id = 32 : i64, hierarchical_name = "top.j.a.U", name = "U", semantic_type = !bit} {
                }
              }
            }
            obelisk.sv.symbol.instance @b attributes {node_id = 33 : i64, hierarchical_name = "top.j.b", is_uninstantiated = false, name = "b", referenced_path = "child", referenced_symbol = @child} {
              obelisk.sv.symbol.instance_body @b_body attributes {node_id = 34 : i64, hierarchical_name = "top.j.b", name = "child"} {
                obelisk.sv.symbol.parameter @N attributes {node_id = 35 : i64, hierarchical_name = "top.j.b.N", name = "N", semantic_type = !int, constant_value = "7"} {
                }
                obelisk.sv.symbol.type_parameter @U attributes {node_id = 36 : i64, hierarchical_name = "top.j.b.U", name = "U", semantic_type = !bit} {
                }
              }
            }
            obelisk.sv.source.parameter_actual attributes {node_id = 37 : i64, parameter = @root::@top::@top_body::@j::@j_body::@a::@a_body::@N, ordinal = 0 : i64, is_named = unit} {
              obelisk.sv.source.parameter_reference attributes {node_id = 38 : i64, parameter = @P} {
              }
            }
            obelisk.sv.source.parameter_actual attributes {node_id = 39 : i64, parameter = @root::@top::@top_body::@j::@j_body::@b::@b_body::@N, ordinal = 1 : i64} {
              obelisk.sv.expression.integer_literal attributes {node_id = 40 : i64, constant_value = "7", is_signed = true, semantic_type = !int} {
              }
            }
            obelisk.sv.source.parameter_actual attributes {node_id = 41 : i64, parameter = @root::@top::@top_body::@j::@j_body::@a::@a_body::@U, ordinal = 2 : i64} {
              obelisk.sv.source.type_reference_default attributes {node_id = 42 : i64, parameter = @T} {
              }
            }
          }
        }
      }
    }
    obelisk.sv.symbol.instance @other attributes {node_id = 501 : i64, hierarchical_name = "other", is_uninstantiated = false, name = "other", referenced_path = "other", referenced_symbol = @other} {
      obelisk.sv.symbol.instance_body @other_body attributes {node_id = 502 : i64, hierarchical_name = "other", name = "other"} {
      }
    }
    obelisk.sv.symbol.instance @loose attributes {node_id = 43 : i64, hierarchical_name = "loose", is_uninstantiated = false, name = "loose", referenced_path = "ip", referenced_symbol = @ip} {
      obelisk.sv.symbol.instance_body @loose_body attributes {node_id = 44 : i64, hierarchical_name = "loose", name = "ip"} {
        obelisk.sv.symbol.parameter @P attributes {node_id = 45 : i64, hierarchical_name = "loose.P", name = "P", semantic_type = !int, constant_value = "19"} {
        }
        obelisk.sv.symbol.type_parameter @T attributes {node_id = 46 : i64, hierarchical_name = "loose.T", name = "T", semantic_type = !byte} {
        }
        obelisk.sv.symbol.instance @a attributes {node_id = 47 : i64, hierarchical_name = "loose.a", is_uninstantiated = false, name = "a", referenced_path = "child", referenced_symbol = @child} {
          obelisk.sv.symbol.instance_body @a_body attributes {node_id = 48 : i64, hierarchical_name = "loose.a", name = "child"} {
            obelisk.sv.symbol.parameter @N attributes {node_id = 49 : i64, hierarchical_name = "loose.a.N", name = "N", semantic_type = !int, constant_value = "19"} {
            }
            obelisk.sv.symbol.type_parameter @U attributes {node_id = 50 : i64, hierarchical_name = "loose.a.U", name = "U", semantic_type = !byte} {
            }
          }
        }
        obelisk.sv.symbol.instance @b attributes {node_id = 51 : i64, hierarchical_name = "loose.b", is_uninstantiated = false, name = "b", referenced_path = "child", referenced_symbol = @child} {
          obelisk.sv.symbol.instance_body @b_body attributes {node_id = 52 : i64, hierarchical_name = "loose.b", name = "child"} {
            obelisk.sv.symbol.parameter @N attributes {node_id = 53 : i64, hierarchical_name = "loose.b.N", name = "N", semantic_type = !int, constant_value = "7"} {
            }
            obelisk.sv.symbol.type_parameter @U attributes {node_id = 54 : i64, hierarchical_name = "loose.b.U", name = "U", semantic_type = !byte} {
            }
          }
        }
        obelisk.sv.source.parameter_actual attributes {node_id = 55 : i64, parameter = @root::@loose::@loose_body::@a::@a_body::@N, ordinal = 0 : i64, is_named = unit} {
          obelisk.sv.source.parameter_reference attributes {node_id = 56 : i64, parameter = @P} {
          }
        }
        obelisk.sv.source.parameter_actual attributes {node_id = 57 : i64, parameter = @root::@loose::@loose_body::@b::@b_body::@N, ordinal = 1 : i64} {
          obelisk.sv.expression.integer_literal attributes {node_id = 58 : i64, constant_value = "7", is_signed = true, semantic_type = !int} {
          }
        }
        obelisk.sv.source.parameter_actual attributes {node_id = 59 : i64, parameter = @root::@loose::@loose_body::@a::@a_body::@U, ordinal = 2 : i64} {
          obelisk.sv.source.type_reference_default attributes {node_id = 60 : i64, parameter = @T} {
          }
        }
      }
    }
  }
}
