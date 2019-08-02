#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define u32 uint32_t
#define u64 uint64_t

enum port_num {
  prim, aux1, aux2, INFO
};

enum class node_typ_nominal {
  era, con, op1, op2, ite, num
};

// Scoped enums has fixed underlying type, thus
// GCC warns
enum /* class */ node_typ {
  NOD, OP1, OP2, ITE
};

constexpr unsigned floorlog2(unsigned x)
{
  return x == 1 ? 0 : 1 + floorlog2(x >> 1);
}

constexpr unsigned num_of_bits_required(unsigned x)
{
  return x == 1 ? 0 : floorlog2(x - 1) + 1;
}

constexpr unsigned num_of_bits_4_node_typ = num_of_bits_required(ITE + 1);

struct config_u32array {
  typedef u64 PortPtrTy;
  typedef u32 NodePtrTy;
  typedef u32 StorageTy;
  typedef u64 ValTy;
  typedef u32 KindTy;

  static constexpr ValTy num_magic = 0x1'00'00'00'00;

  // typedef NodePtrTy NetTy[];
  struct NetTy {
    StorageTy *nodes;
    u32 nodes_len;
    NodePtrTy *redex;
    u32 redex_len;
    NodePtrTy *freed;
    u32 freed_len;
  } Net;

  static constexpr unsigned num_of_bits_4_label = sizeof(StorageTy) * 8 - num_of_bits_4_node_typ - INFO;

  struct info {
    KindTy kind : num_of_bits_4_label;
    node_typ type : num_of_bits_4_node_typ;
    unsigned is_num : INFO;
  };

  static_assert(sizeof(info) == sizeof(StorageTy), "info size should be of regular storage size!");

  static
  const info & as_info(const StorageTy & v){
    return *reinterpret_cast<const info *>(&v);
  }

  static
  info & as_info(StorageTy & v){
    return *reinterpret_cast<info *>(&v);
  }

  static constexpr StorageTy node_size = (INFO + 1);

  // port_of_node
  template<port_num slot> static
  constexpr PortPtrTy Pointer(const NodePtrTy & addr){
    return PortPtrTy(addr * node_size) + PortPtrTy(slot);
  }

  // +node_of_port
  static
  constexpr NodePtrTy addr_of(const PortPtrTy & ptrn) {
    return NodePtrTy(ptrn / node_size);
  }

  // +port_of_port_addr
  static
  constexpr port_num slot_of(const PortPtrTy & ptrn) {
    return port_num(ptrn % node_size);
  }

  // Numbers are immediate and occupy no space, indicated
  // by 3 bits of info
  // +encode_num
  static
  constexpr ValTy Numeric(const StorageTy & nd){
    return ValTy{nd} | num_magic;
  }

  // +decode_num
  static
  constexpr StorageTy numb_of(const ValTy & ne){
    return (StorageTy)ne;
  }

  // -type_of
  static
  constexpr bool value_is_number(const ValTy & p){
    return p >= num_magic;
  }

  // FIXME???
  // I'm not sure pointing to itself is good, since it may sweep problems under the carpet (???)
  // wouldn't something like StorageTy(-1), perhaps, be better to expose potential problems
  static
  void clean_ports_with_info(NetTy & net, const NodePtrTy & addr, info i){
    const NodePtrTy p0 = addr * node_size;
    StorageTy* const this_node = net.nodes + p0;
    this_node[prim] = p0 + prim;
    this_node[aux1] = p0 + aux1;
    this_node[aux2] = p0 + aux2;
    as_info(this_node[INFO]) = i;
  }

  /* Not yet
  template<node_typ type, KindTy kind> static
  NodePtrTy alloc_node(NetTy & net) {
    NodePtrTy addr;
    if (net.freed_len > 0) {
      addr = net.freed[--net.freed_len];
    } else {
      addr = net.nodes_len / node_size;
      net.nodes_len += node_size;
    }
    // FIXME??? Explicit node structure + bitfields?
    // clean_ports_with_info(net, addr, (kind << 6) + (type << 3));
    clean_ports_with_info(net, addr, info{kind, type, 0});
    return addr;
  }
  */

  static
  NodePtrTy alloc_node(NetTy & net, node_typ type, KindTy kind) {
    NodePtrTy addr;
    if (net.freed_len > 0) {
      addr = net.freed[--net.freed_len];
    } else {
      addr = net.nodes_len / node_size;
      net.nodes_len += node_size;
    }
    // FIXME??? Explicit node structure + bitfields?
    // clean_ports_with_info(net, addr, (kind << 6) + (type << 3));
    clean_ports_with_info(net, addr, info{kind, type, 0});
    return addr;
  }

  static void free_node(NetTy & net, const NodePtrTy & addr) {
    // clean_ports_with_info(net, addr, 0);
    clean_ports_with_info(net, addr, info{0, NOD, 0});
  }

  static constexpr bool is_free(const NetTy & net, const NodePtrTy & addr) {
    const PortPtrTy p0 = addr * node_size;
    const StorageTy* const this_node = net.nodes + p0;
    return
         this_node[prim] == p0 + prim
      && this_node[aux1] == p0 + aux1
      && this_node[aux2] == p0 + aux2
      && this_node[INFO] == 0;
  }

  template<port_num slot> static
  constexpr bool is_numeric(const NetTy & net, const NodePtrTy & addr) {
    return (net.nodes[addr * node_size + INFO] >> slot) & 1;
  }

  static
  bool is_numeric(const NetTy & net, const PortPtrTy & ptrn) {
    return (as_info(net.nodes[ptrn]).is_num >> slot_of(ptrn)) & 1;
  }

  // +set_port_value
  // FIXME, slot isn't constexpr yet
  // template<port_num slot> static
  // void set_port(NetTy & net, const NodePtrTy & addr, const ValTy & ptrn) {
  static
  void set_port(NetTy & net, const NodePtrTy & addr, port_num slot, const ValTy & ptrn) {
    const PortPtrTy p0 = addr * node_size;
    if (value_is_number(ptrn)) {
      net.nodes[p0 + slot] = numb_of(ptrn);
      // net.nodes[p0 + INFO] |= (1 << slot);
      as_info(net.nodes[p0 + INFO]).is_num |= (1 << slot); // equivalent but more portable
    } else {
      net.nodes[p0 + slot] = StorageTy(ptrn);
      // net.nodes[p0 + INFO] &= ~(1 << slot);
      as_info(net.nodes[p0 + INFO]).is_num &= ~(1 << slot); // equivalent but more portable
    }
  }

  // FIXME??? Harmonize types ValTy vs PortPtrTy
  template<port_num slot> static
  constexpr ValTy get_port(const NetTy & net, const NodePtrTy & addr) {
    StorageTy val = net.nodes[addr * node_size + slot];
    return is_numeric<slot>(net, addr) ? Numeric(val) : ValTy(val);
  }

  static
  ValTy get_port(const NetTy & net, const PortPtrTy & ptrn) {
    StorageTy val = net.nodes[ptrn];
    return is_numeric(net, ptrn) ? Numeric(val) : ValTy(val);
  }

  template<node_typ ntype> static
  void set_type(NetTy & net, const NodePtrTy & addr) {
    // net.nodes[addr * node_size + INFO] = (net->nodes[addr * node_size + INFO] & ~0b111000) | (type << 3);
    as_info(net.nodes[addr * node_size + INFO]).type = ntype;
  }

  static
  node_typ get_type(NetTy & net, const NodePtrTy & addr) {
    return as_info(net.nodes[addr * node_size + INFO]).type;
  }

  static
  KindTy get_kind(NetTy & net, const NodePtrTy & addr) {
    return as_info(net.nodes[addr * node_size + INFO]).kind;
  }

  static
  ValTy enter_port(NetTy & net, const PortPtrTy & ptrn) {
    if (value_is_number(ptrn)) {
      printf("[ERROR]\nCan't enter a numeric pointer.");
      return 0;
    } else {
      return get_port(net, ptrn);
    }
  }

  static
  bool is_redex(NetTy & net, const NodePtrTy & addr) {
    PortPtrTy a_ptrn = Pointer<prim>(addr);
    PortPtrTy b_ptrn = enter_port(net, a_ptrn);
    return value_is_number(b_ptrn) || (slot_of(b_ptrn) == prim && !is_free(net, addr));
  }

  static
  void link_ports(NetTy & net, const PortPtrTy & a_ptrn, const PortPtrTy & b_ptrn) {
    bool a_numb = value_is_number(a_ptrn);
    bool b_numb = value_is_number(b_ptrn);

    // Point ports to each-other
    if (!a_numb) set_port(net, addr_of(a_ptrn), slot_of(a_ptrn), b_ptrn);
    if (!b_numb) set_port(net, addr_of(b_ptrn), slot_of(b_ptrn), a_ptrn);

    // If both are main ports, add this to the list of active pairs
    if (!(a_numb && b_numb) && (a_numb || slot_of(a_ptrn) == 0) && (b_numb || slot_of(b_ptrn) == 0)) {
      net.redex[net.redex_len++] = a_numb ? addr_of(b_ptrn) : addr_of(a_ptrn);
    }
  }
};

// Impossible ATM
#define CONSTEXPR

template <typename cfg>
CONSTEXPR
void rewrite(typename cfg::NetTy & net, const typename cfg::NodePtrTy & a_addr) {
  #define POINTER(a, p) cfg::template Pointer<p>(a)

  CONSTEXPR auto b_ptrn = cfg::template get_port<prim>(net, a_addr);

  if CONSTEXPR (cfg::value_is_number(b_ptrn)) {
    auto a_type = cfg::get_type(net, a_addr);
    auto a_kind = cfg::get_kind(net, a_addr);

    // UnaryOperation
    if (a_type == OP1) {
      auto dst = cfg::enter_port(net, POINTER(a_addr, aux2));
      auto fst = cfg::numb_of(b_ptrn);
      auto snd = cfg::numb_of(cfg::enter_port(net, POINTER(a_addr, aux1)));
      typename cfg::ValTy res;
      switch (a_kind) {
        case  0: res = cfg::Numeric(fst + snd); break;
        case  1: res = cfg::Numeric(fst - snd); break;
        case  2: res = cfg::Numeric(fst * snd); break;
        case  3: res = cfg::Numeric(fst / snd); break;
        case  4: res = cfg::Numeric(fst % snd); break;
        case  5: res = cfg::Numeric((u32)(pow((float)fst, (float)snd))); break;                   // FIXME??? u32
        case  6: res = cfg::Numeric((u32)(pow((float)fst, ((float)snd / pow(2.0,32.0))))); break; // FIXME??? u32
        case  7: res = cfg::Numeric(fst & snd); break;
        case  8: res = cfg::Numeric(fst | snd); break;
        case  9: res = cfg::Numeric(fst ^ snd); break;
        case 10: res = cfg::Numeric(~snd); break;
        case 11: res = cfg::Numeric(fst >> snd); break;
        case 12: res = cfg::Numeric(fst << snd); break;
        case 13: res = cfg::Numeric(fst > snd ? 1 : 0); break;
        case 14: res = cfg::Numeric(fst < snd ? 1 : 0); break;
        case 15: res = cfg::Numeric(fst == snd ? 1 : 0); break;
        default: res = 0; printf("[ERROR]\nInvalid interaction."); break;
      }
      cfg::link_ports(net, dst, res);
      // cfg::unlink_port(net, Pointer(a_addr, 0));
      // cfg::unlink_port(net, Pointer(a_addr, 2));
      cfg::free_node(net, a_addr);

    // BinaryOperation
    } else if (a_type == OP2) {
      cfg::template set_type<OP1>(net, a_addr);
      cfg::link_ports(net, POINTER(a_addr, prim), cfg::enter_port(net, POINTER(a_addr, aux1)));
      // unlink_port(net, Pointer(a_addr, 1));
      cfg::link_ports(net, POINTER(a_addr, aux1), b_ptrn);

    // NumberDuplication
    } else if (a_type == NOD) {
      cfg::link_ports(net, b_ptrn, cfg::enter_port(net, POINTER(a_addr, aux1)));
      cfg::link_ports(net, b_ptrn, cfg::enter_port(net, POINTER(a_addr, aux2)));
      cfg::free_node(net, a_addr);

    // IfThenElse
    } else if (a_type == ITE) {
      bool cond_val = cfg::numb_of(b_ptrn) == 0;
      auto pair_ptr = cfg::enter_port(net, POINTER(a_addr, aux1));
      cfg::template set_type<NOD>(net, a_addr);
      cfg::link_ports(net, POINTER(a_addr, prim), pair_ptr);
      // unlink_port(net, Pointer(a_addr, 1));
      auto dest_ptr = cfg::enter_port(net, POINTER(a_addr, aux2));
      auto cond_ptr = [&](bool c) { return c ? POINTER(a_addr, aux1) : POINTER(a_addr, aux2); };
      cfg::link_ports(net, cond_ptr(!cond_val), dest_ptr);
        // if (!cond_val) unlink_port(net, Pointer(a_addr, 2));
      auto ptr = cond_ptr(cond_val);
      cfg::link_ports(net, ptr, ptr);

    } else {
      printf("[ERROR]\nInvalid interaction.");
    }

  } else {
    auto b_addr = cfg::addr_of(b_ptrn);
    auto a_type = cfg::get_type(net, a_addr);
    auto b_type = cfg::get_type(net, b_addr);
    auto a_kind = cfg::get_kind(net, a_addr);
    auto b_kind = cfg::get_kind(net, b_addr);

    // NodeAnnihilation, UnaryAnnihilation, BinaryAnnihilation
    if ( (a_type == NOD && b_type == NOD && a_kind == b_kind)
      || (a_type == OP1 && b_type == OP1)
      || (a_type == OP2 && b_type == OP2)
      || (a_type == ITE && b_type == ITE)) {
      auto a_aux1_dest = cfg::enter_port(net, POINTER(a_addr, aux1));
      auto b_aux1_dest = cfg::enter_port(net, POINTER(b_addr, aux1));
      cfg::link_ports(net, a_aux1_dest, b_aux1_dest);
      auto a_aux2_dest = cfg::enter_port(net, POINTER(a_addr, aux2));
      auto b_aux2_dest = cfg::enter_port(net, POINTER(b_addr, aux2));
      cfg::link_ports(net, a_aux2_dest, b_aux2_dest);
      // for (u32 i = 0; i < 3; i++) {
      //   unlink_port(net, Pointer(a_addr, i));
      //   unlink_port(net, Pointer(b_addr, i));
      // }
      cfg::free_node(net, a_addr);
      if (a_addr != b_addr) {
        cfg::free_node(net, b_addr);
      }

    // NodeDuplication, BinaryDuplication
    } else if
      (  (a_type == NOD && b_type == NOD && a_kind != b_kind)
      || (a_type == NOD && b_type == OP2)
      || (a_type == NOD && b_type == ITE)) {
#ifdef ORIGINAL_DUP
      auto p_addr = cfg::alloc_node(net, b_type, b_kind);
      auto q_addr = cfg::alloc_node(net, b_type, b_kind);
      auto r_addr = cfg::alloc_node(net, a_type, a_kind);
      auto s_addr = cfg::alloc_node(net, a_type, a_kind);
      cfg::link_ports(net, POINTER(r_addr, aux1), POINTER(p_addr, aux1));                        // 0
      cfg::link_ports(net, POINTER(s_addr, aux1), POINTER(p_addr, aux2));                        // 1
      cfg::link_ports(net, POINTER(r_addr, aux2), POINTER(q_addr, aux1));                        // 2
      cfg::link_ports(net, POINTER(s_addr, aux2), POINTER(q_addr, aux2));                        // 3
      cfg::link_ports(net, POINTER(p_addr, prim), cfg::enter_port(net, POINTER(a_addr, aux1)));  // 4
      cfg::link_ports(net, POINTER(q_addr, prim), cfg::enter_port(net, POINTER(a_addr, aux2)));  // 5
      cfg::link_ports(net, POINTER(r_addr, prim), cfg::enter_port(net, POINTER(b_addr, aux1)));  // 6
      cfg::link_ports(net, POINTER(s_addr, prim), cfg::enter_port(net, POINTER(b_addr, aux2)));  // 7
      // for (u32 i = 0; i < 3; i++) {
      //   unlink_port(net, Pointer(a_addr, i));
      //   unlink_port(net, Pointer(b_addr, i));
      // }
      cfg::free_node(net, a_addr);
      if (a_addr != b_addr) {
        cfg::free_node(net, b_addr);
      }
#else
      auto p_addr = cfg::alloc_node(net, b_type, b_kind);
      auto r_addr = cfg::alloc_node(net, a_type, a_kind);

      // Reuse b for q, a for s
      #define q_addr b_addr
      #define s_addr a_addr

      // below the lists of all available ports are laid down for each step (prepended with ++),
      // original steps are simply copypasted

      //++ a0, b0, p0, p1, p2, r0, r1, r2
      cfg::link_ports(net, POINTER(p_addr, prim), cfg::enter_port(net, POINTER(a_addr, aux1)));  // 4
      //++ a0, b0, a1, p1, p2, r0, r1, r2
      cfg::link_ports(net, POINTER(q_addr, prim), cfg::enter_port(net, POINTER(a_addr, aux2)));  // 5 b_addr is reused instead of q_addr (b0 is available)
      //++ a0, a2, a1, p1, p2, r0, r1, r2
      cfg::link_ports(net, POINTER(r_addr, prim), cfg::enter_port(net, POINTER(b_addr, aux1)));  // 6
      //++ a0, a2, a1, p1, p2, b1, r1, r2
      cfg::link_ports(net, POINTER(s_addr, prim), cfg::enter_port(net, POINTER(b_addr, aux2)));  // 7 a_addr is reused instead of s_addr (a0 is available)
      //++ b2, a2, a1, p1, p2, b1, r1, r2
      cfg::link_ports(net, POINTER(r_addr, aux1), POINTER(p_addr, aux1));                        // 0
      //++ b2, a2, a1, p2, b1, r2
      cfg::link_ports(net, POINTER(s_addr, aux1), POINTER(p_addr, aux2));                        // 1 a_addr is reused instead of s_addr (a1 is available)
      //++ b2, a2, b1, r2
      cfg::link_ports(net, POINTER(r_addr, aux2), POINTER(q_addr, aux1));                        // 2 b_addr is reused instead of q_addr (b1 is available)
      //++ b2, a2
      cfg::link_ports(net, POINTER(s_addr, aux2), POINTER(q_addr, aux2));                        // 3 a_addr is reused instead of s_addr b_addr is reused instead of q_addr
#endif
    // UnaryDuplication
    } else if
      (  (a_type == NOD && b_type == OP1)
      || (a_type == ITE && b_type == OP1)) {
#ifdef ORIGINAL_DUP
      auto p_addr = cfg::alloc_node(net, b_type, b_kind);
      auto q_addr = cfg::alloc_node(net, b_type, b_kind);
      auto s_addr = cfg::alloc_node(net, a_type, a_kind);
      cfg::link_ports(net, POINTER(p_addr, aux1), cfg::enter_port(net, POINTER(b_addr, aux1)));  // 0
      cfg::link_ports(net, POINTER(q_addr, aux1), cfg::enter_port(net, POINTER(b_addr, aux1)));  // 1
      cfg::link_ports(net, POINTER(s_addr, aux1), POINTER(p_addr, aux2));                        // 2
      cfg::link_ports(net, POINTER(s_addr, aux2), POINTER(q_addr, aux2));                        // 3
      cfg::link_ports(net, POINTER(p_addr, prim), cfg::enter_port(net, POINTER(a_addr, aux1)));  // 4
      cfg::link_ports(net, POINTER(q_addr, prim), cfg::enter_port(net, POINTER(a_addr, aux2)));  // 5
      cfg::link_ports(net, POINTER(s_addr, prim), cfg::enter_port(net, POINTER(b_addr, aux2)));  // 6
      // for (u32 i = 0; i < 3; i++) {
      //   unlink_port(net, Pointer(a_addr, i));
      //   unlink_port(net, Pointer(b_addr, i));
      // }
      cfg::free_node(net, a_addr);
      if (a_addr != b_addr) {
        cfg::free_node(net, b_addr);
      }
#else
      auto p_addr = cfg::alloc_node(net, b_type, b_kind);

      // Reuse b for q, a for s (defined above ^)

      // below the lists of all available ports are laid down for each step (prepended with ++),
      // original steps are simply copypasted

      //++ a0, b0, p0, p1, p2
      cfg::link_ports(net, POINTER(p_addr, prim), cfg::enter_port(net, POINTER(a_addr, aux1)));  // 4
      //++ a0, b0, a1, p1, p2
      cfg::link_ports(net, POINTER(q_addr, prim), cfg::enter_port(net, POINTER(a_addr, aux2)));  // 5 b_addr is reused instead of q_addr (b0 is available)
      //++ a0, a2, a1, p1, p2
      cfg::link_ports(net, POINTER(s_addr, prim), cfg::enter_port(net, POINTER(b_addr, aux2)));  // 6 a_addr is reused instead of s_addr (a0 is available)
      //++ b2, a2, a1, p1, p2
      cfg::link_ports(net, POINTER(p_addr, aux1), cfg::enter_port(net, POINTER(b_addr, aux1)));  // 0
      //++ b2, a2, a1, b1, p2
      // cfg::link_ports(net, POINTER(q_addr, aux1), cfg::enter_port(net, POINTER(b_addr, aux1)));  // 1 collapsed since b is q
      //++ b2, a2, a1, p2
      cfg::link_ports(net, POINTER(s_addr, aux1), POINTER(p_addr, aux2));                        // 2 a_addr is reused instead of s_addr (a1 is available)
      //++ b2, a2
      cfg::link_ports(net, POINTER(s_addr, aux2), POINTER(q_addr, aux2));                        // 3 a_addr is reused instead of s_addr b_addr is reused instead of q_addr

      #undef q_addr
      #undef s_addr
#endif
    // Permutations
    } else if (a_type == OP1 && b_type == NOD) {
      return rewrite<cfg>(net, b_addr);
    } else if (a_type == OP2 && b_type == NOD) {
      return rewrite<cfg>(net, b_addr);
    } else if (a_type == ITE && b_type == NOD) {
      return rewrite<cfg>(net, b_addr);

    // InvalidInteraction
    } else {
      printf("[ERROR]\nInvalid interaction.");
    }
  }
}

CONSTEXPR
void rewrite_u32array(config_u32array::NetTy &net, const u32 & a_addr) {
  rewrite<config_u32array>(net, a_addr);
}

#if 0
// Rewrites active pairs until none is left, reducing the graph to normal form
// This could be performed in parallel. Unreachable data is freed automatically.
Stats reduce(Net *net) {
  Stats stats;
  stats.rewrites = 0;
  stats.loops = 0;
  while (net->redex_len > 0) {
    for (u32 i = 0, l = net->redex_len; i < l; ++i) {
      rewrite(net, net->redex[--net->redex_len]);
      ++stats.rewrites;
    }
    ++stats.loops;
  }
  return stats;
}

void find_redexes(Net *net) {
  net->redex_len = 0;
  for (u32 i = 0; i < net->nodes_len / 4; ++i) {
    u64 b_ptrn = enter_port(net, Pointer(i, 0));
    if ((type_of(b_ptrn) == NUM || addr_of(b_ptrn) >= i) && is_redex(net, i)) {
      net->redex[net->redex_len++] = i;
    }
  }
}

void print_pointer(u64 ptrn) {
  if (type_of(ptrn) == NUM) {
    printf("#%u", numb_of(ptrn));
  } else {
    printf("%u", addr_of(ptrn));
    switch (slot_of(ptrn)) {
      case 0: printf("a"); break;
      case 1: printf("b"); break;
      case 2: printf("c"); break;
    }
  }
}

void print_net(Net* net) {
  for (u32 i = 0; i < net->nodes_len / 4; i++) {
    if (is_free(net, i)) {
      printf("%u: ~\n", i);
    } else {
      u32 type = get_type(net, i);
      u32 kind = get_kind(net, i);
      printf("%u: ", i);
      printf("[%u:%u| ", type, kind);
      print_pointer(get_port(net, i, 0));
      printf(" ");
      print_pointer(get_port(net, i, 1));
      printf(" ");
      print_pointer(get_port(net, i, 2));
      printf("]");
      printf("...");
      printf("%d ", is_numeric(net, i, 0));
      printf("%d ", is_numeric(net, i, 1));
      printf("%d ", is_numeric(net, i, 2));
      printf("\n");
    }
  }
}

// def keccak: {s} (bytes_to_hex (Keccak256 (to_chars s)))
// def main: dup fn = (~512 #keccak) # (fn "")
// Applies keccak256 1024 times to empty string
static u32 nodes[] = {
  2,8466,0,0,8,164,8464,0,4,24,156,0,40,21,18,0,25,22,14,0,26,13,17,0,9,16,20,64,56,37,34,0,41,38,30,0,42,29,33,0,12,32,36,64,72,53,50,0,57,54,46,0,58,45,49,0,28,48,52,64,88,69,66,0,73,70,62,0,74,61,65,0,44,64,68,64,104,85,82,0,89,86,78,0,90,77,81,0,60,80,84,64,120,101,98,0,105,102,94,0,106,93,97,0,76,96,100,64,136,117,114,0,121,118,110,0,122,109,113,0,92,112,116,64,152,133,130,0,137,134,126,0,138,125,129,0,108,128,132,64,160,149,146,0,153,150,142,0,154,141,145,0,124,144,148,64,10,161,162,0,140,157,158,0,5,8229,170,0,172,450,166,0,168,324,178,0,180,320,174,0,176,192,184,0,182,1954047348,188,4194242,186,316,284,0,181,196,296,0,193,221,200,0,198,256,204,0,202,217,208,0,206,280,234,0,222,218,272,528,8,205,213,721,24,197,212,785,3,230,264,977,4,281,225,273,238,254,210,0,265,240,232,4194264,237,246,248,4194240,317,273,241,0,242,250,249,0,258,278,233,0,201,262,252,0,266,268,257,4194264,226,236,260,4194240,261,0,274,4194242,214,245,270,4194240,1,282,253,17,209,229,277,4194240,190,309,290,0,294,0,286,2,298,0,288,2,194,300,292,0,297,313,304,0,302,305,310,0,314,285,306,0,318,301,308,0,189,244,312,128,177,392,440,0,173,328,444,0,325,436,332,0,330,385,338,0,342,386,334,0,393,346,336,0,348,382,341,0,344,376,354,0,358,360,350,4194264,10,369,352,913,353,366,374,4194240,48,370,361,17,377,357,365,4194240,87,378,362,17,349,368,373,4194240,16,437,345,209,390,333,337,0,394,398,384,0,321,340,388,128,400,434,389,0,396,428,406,0,410,412,402,4194264,10,421,404,913,405,418,426,4194240,48,422,413,17,429,409,417,4194240,87,430,414,17,401,420,425,4194240,16,438,397,273,329,381,433,4194240,322,445,446,0,326,441,442,0,452,8230,169,0,448,456,794,0,453,460,808,0,457,505,464,0,462,501,468,0,466,788,472,0,470,493,476,0,474,489,480,0,478,485,486,0,490,481,482,0,494,477,484,0,498,473,488,0,502,790,492,0,506,465,496,0,510,461,500,0,514,520,504,4194264,135,518,508,977,136,789,513,273,509,524,684,4194240,521,665,528,0,526,560,532,0,530,565,536,0,534,573,540,0,538,609,544,0,542,680,550,0,554,682,546,0,558,668,548,0,562,570,552,0,529,566,556,0,1,533,561,17,681,574,557,0,578,537,569,0,580,610,572,0,576,600,584,0,582,592,588,0,586,601,594,0,585,598,590,0,602,604,593,0,581,589,596,0,597,606,605,0,614,541,577,0,616,660,608,0,612,656,620,0,618,636,624,0,622,637,628,0,626,648,634,0,638,640,630,0,621,625,632,0,633,652,644,0,642,657,650,0,629,654,646,0,641,658,649,0,617,645,653,0,613,664,666,0,661,525,662,592,553,677,672,0,670,676,678,0,673,669,674,0,545,568,549,192,522,785,688,0,686,720,692,0,690,725,696,0,694,717,700,0,698,729,704,0,702,709,710,0,714,705,706,0,718,730,708,0,722,697,712,0,689,726,716,0,1,693,721,17,734,701,713,0,736,780,728,0,732,776,740,0,738,756,744,0,742,757,748,0,746,768,754,0,758,760,750,0,741,745,752,0,753,772,764,0,762,777,770,0,749,774,766,0,761,778,769,0,737,765,773,0,733,784,786,0,781,685,782,592,469,517,497,4194240,798,3620,454,0,802,3608,792,0,806,3394,796,0,810,0,800,2,458,812,804,0,809,813,816,0,814,829,820,0,818,865,824,0,822,1168,1174,0,834,817,1160,0,836,866,828,0,832,856,840,0,838,848,844,0,842,857,850,0,841,854,846,0,858,860,849,0,837,845,852,0,853,862,861,0,870,821,833,0,872,916,864,0,868,912,876,0,874,892,880,0,878,893,884,0,882,904,890,0,894,896,886,0,877,881,888,0,889,908,900,0,898,913,906,0,885,910,902,0,897,914,905,0,873,901,909,0,869,921,922,0,1,917,918,593,930,1152,1164,0,934,1132,924,0,938,1116,928,0,940,968,932,0,936,956,944,0,942,965,948,0,946,957,952,0,950,960,962,0,941,949,964,0,953,966,954,0,958,945,961,0,937,1092,1084,0,1100,981,978,0,985,982,974,0,986,973,977,0,1093,976,980,192,1108,997,994,0,1001,998,990,0,1002,989,993,0,1101,992,996,192,1032,1013,1010,0,1017,1014,1006,0,1018,1005,1009,0,1109,1008,1012,192,1048,1029,1026,0,1033,1030,1022,0,1034,1021,1025,0,1004,1024,1028,192,1064,1045,1042,0,1049,1046,1038,0,1050,1037,1041,0,1020,1040,1044,192,1080,1061,1058,0,1065,1062,1054,0,1066,1053,1057,0,1036,1056,1060,192,1112,1077,1074,0,1081,1078,1070,0,1082,1069,1073,0,1052,1072,1076,192,970,1113,1090,0,1094,1098,1086,0,969,984,1088,192,1102,1106,1089,0,972,1000,1096,192,1110,1114,1097,0,988,1016,1104,192,1068,1085,1105,0,933,1124,1120,0,1118,1129,1126,0,1117,1130,1122,0,128,1121,1125,593,929,1144,1136,0,1134,1148,1140,0,1138,1149,1146,0,1133,1150,1142,0,1137,1141,1145,0,925,1154,1153,0,1169,1165,1162,0,830,1166,1158,0,926,1157,1161,0,825,1156,1172,0,1170,1176,826,0,1173,1981,1180,0,1178,2025,1184,0,1182,2069,1188,0,1186,2113,1192,0,1190,2157,1196,0,1194,2201,1200,0,1198,2245,1204,0,1202,2289,1208,0,1206,2333,1212,0,1210,2377,1216,0,1214,2421,1220,0,1218,2465,1224,0,1222,2509,1228,0,1226,2553,1232,0,1230,2597,1236,0,1234,2641,1240,0,1238,2685,1244,0,1242,2729,1248,0,1246,2773,1252,0,1250,2817,1256,0,1254,2861,1260,0,1258,2905,1264,0,1262,2949,1268,0,1266,2993,1272,0,1270,3037,1276,0,1274,3081,1280,0,1278,3125,1284,0,1282,3169,1288,0,1286,3213,1292,0,1290,3257,1296,0,1294,3301,1300,0,1298,3345,1304,0,1302,1305,1308,0,1306,1309,1312,0,1310,1313,1316,0,1314,1317,1320,0,1318,1321,1324,0,1322,1325,1328,0,1326,1329,1332,0,1330,1333,1336,0,1334,1337,1340,0,1338,1341,1344,0,1342,1345,1348,0,1346,1349,1352,0,1350,1353,1356,0,1354,1357,1360,0,1358,1361,1364,0,1362,1365,1368,0,1366,1369,1372,0,1370,1373,1376,0,1374,1377,1380,0,1378,1381,1384,0,1382,1385,1388,0,1386,1389,1392,0,1390,1393,1396,0,1394,1397,1400,0,1398,1401,1404,0,1402,1405,1408,0,1406,1409,1412,0,1410,1413,1416,0,1414,1417,1420,0,1418,1421,1424,0,1422,1425,1428,0,1426,1429,1432,0,1430,1433,1436,0,1434,1437,1440,0,1438,1441,1444,0,1442,1445,1448,0,1446,1449,1452,0,1450,1453,1456,0,1454,1457,1460,0,1458,1461,1464,0,1462,1465,1468,0,1466,1469,1472,0,1470,1473,1476,0,1474,1477,1480,0,1478,1481,1484,0,1482,1485,1488,0,1486,1489,1492,0,1490,1493,1496,0,1494,1497,1500,0,1498,1501,1504,0,1502,1505,1508,0,1506,1509,1512,0,1510,1513,1516,0,1514,1517,1520,0,1518,1521,1524,0,1522,1525,1528,0,1526,1529,1532,0,1530,1533,1536,0,1534,1537,1540,0,1538,1541,1544,0,1542,1545,1548,0,1546,1549,1552,0,1550,1553,1556,0,1554,1557,1560,0,1558,1561,1564,0,1562,1565,1568,0,1566,1569,1572,0,1570,1573,1576,0,1574,1577,1580,0,1578,1581,1584,0,1582,1585,1588,0,1586,1589,1592,0,1590,1593,1596,0,1594,1597,1600,0,1598,1601,1604,0,1602,1605,1608,0,1606,1609,1612,0,1610,1613,1616,0,1614,1617,1620,0,1618,1621,1624,0,1622,1625,1628,0,1626,1629,1632,0,1630,1633,1636,0,1634,1637,1640,0,1638,1641,1644,0,1642,1645,1648,0,1646,1649,1652,0,1650,1653,1656,0,1654,1657,1660,0,1658,1661,1664,0,1662,1665,1668,0,1666,1669,1672,0,1670,1673,1676,0,1674,1677,1680,0,1678,1681,1684,0,1682,1685,1688,0,1686,1689,1692,0,1690,1693,1696,0,1694,1697,1700,0,1698,1701,1704,0,1702,1705,1708,0,1706,1709,1712,0,1710,1713,1716,0,1714,1717,1720,0,1718,1721,1724,0,1722,1725,1728,0,1726,1729,1732,0,1730,1733,1736,0,1734,1737,1740,0,1738,1741,1744,0,1742,1745,1748,0,1746,1749,1752,0,1750,1753,1756,0,1754,1757,1760,0,1758,1761,1764,0,1762,1765,1768,0,1766,1769,1772,0,1770,1773,1776,0,1774,1777,1780,0,1778,1781,1784,0,1782,1785,1788,0,1786,1789,1792,0,1790,1793,1796,0,1794,1797,1800,0,1798,1801,1804,0,1802,1805,1808,0,1806,1809,1812,0,1810,1813,1816,0,1814,1817,1820,0,1818,1821,1824,0,1822,1825,1828,0,1826,1829,1832,0,1830,1833,1836,0,1834,1837,1840,0,1838,1841,1844,0,1842,1845,1848,0,1846,1849,1852,0,1850,1853,1856,0,1854,1857,1860,0,1858,1861,1864,0,1862,1865,1868,0,1866,1869,1872,0,1870,1873,1876,0,1874,1877,1880,0,1878,1881,1884,0,1882,1885,1888,0,1886,1889,1892,0,1890,1893,1896,0,1894,1897,1900,0,1898,1901,1904,0,1902,1905,1908,0,1906,1909,1912,0,1910,1913,1916,0,1914,1917,1920,0,1918,1921,1924,0,1922,1925,1928,0,1926,1929,1932,0,1930,1933,1936,0,1934,1937,1940,0,1938,1941,1944,0,1942,1945,1948,0,1946,1949,1952,0,1950,1953,1956,0,1954,1957,1960,0,1958,1961,1964,0,1962,1965,1968,0,1966,1969,1972,0,1970,1973,1978,0,1982,2022,1974,0,1984,1177,1976,0,1980,2009,1988,0,1986,1996,1992,0,1990,2012,2000,0,1989,2013,2016,0,1994,2017,2006,0,2010,2018,2002,0,2014,1985,2004,0,1993,1997,2008,256,1998,2001,2005,0,2026,2066,1977,0,2028,1181,2020,0,2024,2053,2032,0,2030,2040,2036,0,2034,2056,2044,0,2033,2057,2060,0,2038,2061,2050,0,2054,2062,2046,0,2058,2029,2048,0,2037,2041,2052,256,2042,2045,2049,0,2070,2110,2021,0,2072,1185,2064,0,2068,2097,2076,0,2074,2084,2080,0,2078,2100,2088,0,2077,2101,2104,0,2082,2105,2094,0,2098,2106,2090,0,2102,2073,2092,0,2081,2085,2096,256,2086,2089,2093,0,2114,2154,2065,0,2116,1189,2108,0,2112,2141,2120,0,2118,2128,2124,0,2122,2144,2132,0,2121,2145,2148,0,2126,2149,2138,0,2142,2150,2134,0,2146,2117,2136,0,2125,2129,2140,256,2130,2133,2137,0,2158,2198,2109,0,2160,1193,2152,0,2156,2185,2164,0,2162,2172,2168,0,2166,2188,2176,0,2165,2189,2192,0,2170,2193,2182,0,2186,2194,2178,0,2190,2161,2180,0,2169,2173,2184,256,2174,2177,2181,0,2202,2242,2153,0,2204,1197,2196,0,2200,2229,2208,0,2206,2216,2212,0,2210,2232,2220,0,2209,2233,2236,0,2214,2237,2226,0,2230,2238,2222,0,2234,2205,2224,0,2213,2217,2228,256,2218,2221,2225,0,2246,2286,2197,0,2248,1201,2240,0,2244,2273,2252,0,2250,2260,2256,0,2254,2276,2264,0,2253,2277,2280,0,2258,2281,2270,0,2274,2282,2266,0,2278,2249,2268,0,2257,2261,2272,256,2262,2265,2269,0,2290,2330,2241,0,2292,1205,2284,0,2288,2317,2296,0,2294,2304,2300,0,2298,2320,2308,0,2297,2321,2324,0,2302,2325,2314,0,2318,2326,2310,0,2322,2293,2312,0,2301,2305,2316,256,2306,2309,2313,0,2334,2374,2285,0,2336,1209,2328,0,2332,2361,2340,0,2338,2348,2344,0,2342,2364,2352,0,2341,2365,2368,0,2346,2369,2358,0,2362,2370,2354,0,2366,2337,2356,0,2345,2349,2360,256,2350,2353,2357,0,2378,2418,2329,0,2380,1213,2372,0,2376,2405,2384,0,2382,2392,2388,0,2386,2408,2396,0,2385,2409,2412,0,2390,2413,2402,0,2406,2414,2398,0,2410,2381,2400,0,2389,2393,2404,256,2394,2397,2401,0,2422,2462,2373,0,2424,1217,2416,0,2420,2449,2428,0,2426,2436,2432,0,2430,2452,2440,0,2429,2453,2456,0,2434,2457,2446,0,2450,2458,2442,0,2454,2425,2444,0,2433,2437,2448,256,2438,2441,2445,0,2466,2506,2417,0,2468,1221,2460,0,2464,2493,2472,0,2470,2480,2476,0,2474,2496,2484,0,2473,2497,2500,0,2478,2501,2490,0,2494,2502,2486,0,2498,2469,2488,0,2477,2481,2492,256,2482,2485,2489,0,2510,2550,2461,0,2512,1225,2504,0,2508,2537,2516,0,2514,2524,2520,0,2518,2540,2528,0,2517,2541,2544,0,2522,2545,2534,0,2538,2546,2530,0,2542,2513,2532,0,2521,2525,2536,256,2526,2529,2533,0,2554,2594,2505,0,2556,1229,2548,0,2552,2581,2560,0,2558,2568,2564,0,2562,2584,2572,0,2561,2585,2588,0,2566,2589,2578,0,2582,2590,2574,0,2586,2557,2576,0,2565,2569,2580,256,2570,2573,2577,0,2598,2638,2549,0,2600,1233,2592,0,2596,2625,2604,0,2602,2612,2608,0,2606,2628,2616,0,2605,2629,2632,0,2610,2633,2622,0,2626,2634,2618,0,2630,2601,2620,0,2609,2613,2624,256,2614,2617,2621,0,2642,2682,2593,0,2644,1237,2636,0,2640,2669,2648,0,2646,2656,2652,0,2650,2672,2660,0,2649,2673,2676,0,2654,2677,2666,0,2670,2678,2662,0,2674,2645,2664,0,2653,2657,2668,256,2658,2661,2665,0,2686,2726,2637,0,2688,1241,2680,0,2684,2713,2692,0,2690,2700,2696,0,2694,2716,2704,0,2693,2717,2720,0,2698,2721,2710,0,2714,2722,2706,0,2718,2689,2708,0,2697,2701,2712,256,2702,2705,2709,0,2730,2770,2681,0,2732,1245,2724,0,2728,2757,2736,0,2734,2744,2740,0,2738,2760,2748,0,2737,2761,2764,0,2742,2765,2754,0,2758,2766,2750,0,2762,2733,2752,0,2741,2745,2756,256,2746,2749,2753,0,2774,2814,2725,0,2776,1249,2768,0,2772,2801,2780,0,2778,2788,2784,0,2782,2804,2792,0,2781,2805,2808,0,2786,2809,2798,0,2802,2810,2794,0,2806,2777,2796,0,2785,2789,2800,256,2790,2793,2797,0,2818,2858,2769,0,2820,1253,2812,0,2816,2845,2824,0,2822,2832,2828,0,2826,2848,2836,0,2825,2849,2852,0,2830,2853,2842,0,2846,2854,2838,0,2850,2821,2840,0,2829,2833,2844,256,2834,2837,2841,0,2862,2902,2813,0,2864,1257,2856,0,2860,2889,2868,0,2866,2876,2872,0,2870,2892,2880,0,2869,2893,2896,0,2874,2897,2886,0,2890,2898,2882,0,2894,2865,2884,0,2873,2877,2888,256,2878,2881,2885,0,2906,2946,2857,0,2908,1261,2900,0,2904,2933,2912,0,2910,2920,2916,0,2914,2936,2924,0,2913,2937,2940,0,2918,2941,2930,0,2934,2942,2926,0,2938,2909,2928,0,2917,2921,2932,256,2922,2925,2929,0,2950,2990,2901,0,2952,1265,2944,0,2948,2977,2956,0,2954,2964,2960,0,2958,2980,2968,0,2957,2981,2984,0,2962,2985,2974,0,2978,2986,2970,0,2982,2953,2972,0,2961,2965,2976,256,2966,2969,2973,0,2994,3034,2945,0,2996,1269,2988,0,2992,3021,3000,0,2998,3008,3004,0,3002,3024,3012,0,3001,3025,3028,0,3006,3029,3018,0,3022,3030,3014,0,3026,2997,3016,0,3005,3009,3020,256,3010,3013,3017,0,3038,3078,2989,0,3040,1273,3032,0,3036,3065,3044,0,3042,3052,3048,0,3046,3068,3056,0,3045,3069,3072,0,3050,3073,3062,0,3066,3074,3058,0,3070,3041,3060,0,3049,3053,3064,256,3054,3057,3061,0,3082,3122,3033,0,3084,1277,3076,0,3080,3109,3088,0,3086,3096,3092,0,3090,3112,3100,0,3089,3113,3116,0,3094,3117,3106,0,3110,3118,3102,0,3114,3085,3104,0,3093,3097,3108,256,3098,3101,3105,0,3126,3166,3077,0,3128,1281,3120,0,3124,3153,3132,0,3130,3140,3136,0,3134,3156,3144,0,3133,3157,3160,0,3138,3161,3150,0,3154,3162,3146,0,3158,3129,3148,0,3137,3141,3152,256,3142,3145,3149,0,3170,3210,3121,0,3172,1285,3164,0,3168,3197,3176,0,3174,3184,3180,0,3178,3200,3188,0,3177,3201,3204,0,3182,3205,3194,0,3198,3206,3190,0,3202,3173,3192,0,3181,3185,3196,256,3186,3189,3193,0,3214,3254,3165,0,3216,1289,3208,0,3212,3241,3220,0,3218,3228,3224,0,3222,3244,3232,0,3221,3245,3248,0,3226,3249,3238,0,3242,3250,3234,0,3246,3217,3236,0,3225,3229,3240,256,3230,3233,3237,0,3258,3298,3209,0,3260,1293,3252,0,3256,3285,3264,0,3262,3272,3268,0,3266,3288,3276,0,3265,3289,3292,0,3270,3293,3282,0,3286,3294,3278,0,3290,3261,3280,0,3269,3273,3284,256,3274,3277,3281,0,3302,3342,3253,0,3304,1297,3296,0,3300,3329,3308,0,3306,3316,3312,0,3310,3332,3320,0,3309,3333,3336,0,3314,3337,3326,0,3330,3338,3322,0,3334,3305,3324,0,3313,3317,3328,256,3318,3321,3325,0,3346,3384,3297,0,3348,1301,3340,0,3344,3373,3352,0,3350,3360,3356,0,3354,3376,3364,0,3353,3377,3380,0,3358,3381,3370,0,3374,3382,3366,0,3378,3349,3368,0,3357,3361,3372,256,3362,3365,3369,0,3341,3385,3388,0,3386,3390,3389,0,3398,3604,801,0,3402,3582,3392,0,3406,3576,3396,0,3408,3436,3400,0,3404,3424,3412,0,3410,3433,3416,0,3414,3425,3420,0,3418,3428,3430,0,3409,3417,3432,0,3421,3434,3422,0,3426,3413,3429,0,3405,3452,3552,0,3468,3449,3446,0,3453,3450,3442,0,3454,3441,3445,0,3437,3444,3448,192,3484,3465,3462,0,3469,3466,3458,0,3470,3457,3461,0,3440,3460,3464,192,3560,3481,3478,0,3485,3482,3474,0,3486,3473,3477,0,3456,3476,3480,192,3516,3497,3494,0,3501,3498,3490,0,3502,3489,3493,0,3561,3492,3496,192,3532,3513,3510,0,3517,3514,3506,0,3518,3505,3509,0,3488,3508,3512,192,3568,3529,3526,0,3533,3530,3522,0,3534,3521,3525,0,3504,3524,3528,192,3572,3545,3542,0,3549,3546,3538,0,3550,3537,3541,0,3569,3540,3544,192,3438,3573,3558,0,3562,3566,3554,0,3472,3500,3556,192,3570,3574,3557,0,3520,3548,3564,192,3536,3553,3565,0,3401,3578,3577,0,3584,0,3397,2,3580,3601,3588,0,3586,3596,3592,0,3590,3600,3598,0,3589,3602,3594,0,3593,3585,3597,0,3393,3606,3605,0,797,3617,3612,0,3610,3616,3618,0,3613,3609,3614,0,793,3908,8226,0,3630,3892,3912,0,3634,3788,3624,0,3638,3776,3628,0,3640,3668,3632,0,3636,3656,3644,0,3642,3665,3648,0,3646,3657,3652,0,3650,3660,3662,0,3641,3649,3664,0,3653,3666,3654,0,3658,3645,3661,0,3637,3684,3752,0,3760,3681,3678,0,3685,3682,3674,0,3686,3673,3677,0,3669,3676,3680,256,3716,3697,3694,0,3701,3698,3690,0,3702,3689,3693,0,3761,3692,3696,256,3732,3713,3710,0,3717,3714,3706,0,3718,3705,3709,0,3688,3708,3712,256,3768,3729,3726,0,3733,3730,3722,0,3734,3721,3725,0,3704,3724,3728,256,3772,3745,3742,0,3749,3746,3738,0,3750,3737,3741,0,3769,3740,3744,256,3670,3773,3758,0,3762,3766,3754,0,3672,3700,3756,256,3770,3774,3757,0,3720,3748,3764,256,3736,3753,3765,0,3633,3784,3780,0,3778,3785,3786,0,3777,3781,3782,0,3629,3804,3792,0,3790,3828,3796,0,3794,3801,3802,0,3806,3797,3798,0,3789,3808,3800,0,3805,3832,3812,0,3810,3849,3816,0,3814,3845,3820,0,3818,3841,3824,0,3822,3837,3830,0,3793,3834,3826,0,3809,3838,3829,0,3842,3825,3833,0,3846,3821,3836,0,3850,3817,3840,0,3852,3813,3844,0,3848,3869,3856,0,3854,3877,3860,0,3858,3885,3864,0,3862,3889,3870,0,3874,3853,3866,16,8,3878,3868,785,3882,3857,3873,16,8,3886,3876,785,3890,3861,3881,16,8,3865,3884,785,3625,3896,3898,0,3893,3900,3894,0,3897,3902,3901,0,4045,3913,3910,0,3621,3914,3906,0,3626,3905,3909,0,3922,7708,8004,0,3926,4072,3916,0,3930,4046,3920,0,3932,3960,3924,0,3928,3948,3936,0,3934,3957,3940,0,3938,3949,3944,0,3942,3952,3954,0,3933,3941,3956,0,3945,3958,3946,0,3950,3937,3953,0,3929,3976,4028,0,3992,3973,3970,0,3977,3974,3966,0,3978,3965,3969,0,3961,3968,3972,256,4008,3989,3986,0,3993,3990,3982,0,3994,3981,3985,0,3964,3984,3988,256,4036,4005,4002,0,4009,4006,3998,0,4010,3997,4001,0,3980,4000,4004,256,4040,4021,4018,0,4025,4022,4014,0,4026,4013,4017,0,4037,4016,4020,256,3962,4041,4034,0,4038,4042,4030,0,3996,4024,4032,256,4012,4029,4033,0,4050,3904,3925,0,4052,0,4044,2,4048,4069,4056,0,4054,4064,4060,0,4058,4068,4066,0,4057,4070,4062,0,4061,4053,4065,0,3921,4080,4076,0,4074,7696,4082,0,4073,4084,4078,0,4081,7704,4088,0,4086,4492,4092,0,4090,4500,4096,0,4094,4644,4100,0,4098,4652,4104,0,4102,4804,4108,0,4106,4812,4112,0,4110,4972,4116,0,4114,4980,4120,0,4118,5140,4124,0,4122,5148,4128,0,4126,4508,4132,0,4130,4520,4136,0,4134,4660,4140,0,4138,4672,4144,0,4142,4820,4148,0,4146,4832,4152,0,4150,4988,4156,0,4154,5000,4160,0,4158,5156,4164,0,4162,5168,4168,0,4166,4532,4172,0,4170,4544,4176,0,4174,4684,4180,0,4178,4696,4184,0,4182,4844,4188,0,4186,4856,4192,0,4190,5012,4196,0,4194,5024,4200,0,4198,5180,4204,0,4202,5192,4208,0,4206,4556,4212,0,4210,4568,4216,0,4214,4708,4220,0,4218,4720,4224,0,4222,4868,4228,0,4226,4880,4232,0,4230,5036,4236,0,4234,5048,4240,0,4238,5204,4244,0,4242,5216,4248,0,4246,4580,4252,0,4250,4592,4256,0,4254,4732,4260,0,4258,4744,4264,0,4262,4892,4268,0,4266,4904,4272,0,4270,5060,4276,0,4274,5072,4280,0,4278,5228,4284,0,4282,5240,7498,0,4581,4294,5132,592,4557,4298,4289,592,4533,4302,4293,592,4509,4493,4297,592,4593,4310,5124,592,4569,4314,4305,592,4545,4318,4309,592,4521,4501,4313,592,4733,4326,4756,592,4709,4330,4321,592,4685,4334,4325,592,4661,4645,4329,592,4745,4342,4776,592,4721,4346,4337,592,4697,4350,4341,592,4673,4653,4345,592,4893,4358,4916,592,4869,4362,4353,592,4845,4366,4357,592,4821,4805,4361,592,4905,4374,4944,592,4881,4378,4369,592,4857,4382,4373,592,4833,4813,4377,592,5061,4390,5084,592,5037,4394,4385,592,5013,4398,4389,592,4989,4973,4393,592,5073,4406,5112,592,5049,4410,4401,592,5025,4414,4405,592,5001,4981,4409,592,5229,4422,4964,592,5205,4426,4417,592,5181,4430,4421,592,5157,5141,4425,592,5241,4438,4956,592,5217,4442,4433,592,5193,4446,4437,592,5169,5149,4441,592,4454,4929,4584,592,4462,4458,4448,528,1,4485,4453,785,31,4477,4452,721,4470,4937,4596,592,4482,4474,4464,528,1,4478,4469,785,4777,4461,4473,320,31,4486,4468,721,4757,4457,4481,320,4513,4494,6792,592,4089,4301,4489,320,4525,4502,6816,592,4093,4317,4497,320,4514,4510,5268,592,4129,4300,4505,320,4537,4488,4504,320,4526,4522,5276,592,4133,4316,4517,320,4549,4496,4516,320,4538,4534,5308,592,4169,4296,4529,320,4561,4512,4528,320,4550,4546,5300,592,4173,4312,4541,320,4573,4524,4540,320,4562,4558,5332,592,4209,4292,4553,320,4585,4536,4552,320,4574,4570,5340,592,4213,4308,4565,320,4597,4548,4564,320,4586,4582,5372,592,4249,4288,4577,320,4450,4560,4576,320,4598,4594,5364,592,4253,4304,4589,320,4466,4572,4588,320,4606,5097,4736,592,4614,4610,4600,528,1,4637,4605,785,31,4629,4604,721,4622,5105,4748,592,4634,4626,4616,528,1,4630,4621,785,4945,4613,4625,320,31,4638,4620,721,4917,4609,4633,320,4665,4646,5404,592,4097,4333,4641,320,4677,4654,5396,592,4101,4349,4649,320,4666,4662,5428,592,4137,4332,4657,320,4689,4640,4656,320,4678,4674,5436,592,4141,4348,4669,320,4701,4648,4668,320,4690,4686,5468,592,4177,4328,4681,320,4713,4664,4680,320,4702,4698,5460,592,4181,4344,4693,320,4725,4676,4692,320,4714,4710,5492,592,4217,4324,4705,320,4737,4688,4704,320,4726,4722,5500,592,4221,4340,4717,320,4749,4700,4716,320,4738,4734,5532,592,4257,4320,4729,320,4602,4712,4728,320,4750,4746,5524,592,4261,4336,4741,320,4618,4724,4740,320,4762,4758,4896,592,4322,4484,4753,320,4770,4766,4752,528,1,4797,4761,785,31,4789,4760,721,4782,4778,4908,592,4338,4476,4773,320,4794,4786,4772,528,1,4790,4781,785,5113,4769,4785,320,31,4798,4780,721,5085,4765,4793,320,4825,4806,5556,592,4105,4365,4801,320,4837,4814,5564,592,4109,4381,4809,320,4826,4822,5596,592,4145,4364,4817,320,4849,4800,4816,320,4838,4834,5588,592,4149,4380,4829,320,4861,4808,4828,320,4850,4846,5620,592,4185,4360,4841,320,4873,4824,4840,320,4862,4858,5628,592,4189,4376,4853,320,4885,4836,4852,320,4874,4870,5660,592,4225,4356,4865,320,4897,4848,4864,320,4886,4882,5652,592,4229,4372,4877,320,4909,4860,4876,320,4898,4894,5684,592,4265,4352,4889,320,4754,4872,4888,320,4910,4906,5692,592,4269,4368,4901,320,4774,4884,4900,320,4922,4918,5064,592,4354,4636,4913,320,4934,4926,4912,528,1,4930,4921,785,4965,4449,4925,320,31,4938,4920,721,4957,4465,4933,320,4950,4946,5076,592,4370,4628,4941,320,4962,4954,4940,528,1,4958,4949,785,4434,4936,4953,320,31,4966,4948,721,4418,4928,4961,320,4993,4974,5724,592,4113,4397,4969,320,5005,4982,5716,592,4117,4413,4977,320,4994,4990,5748,592,4153,4396,4985,320,5017,4968,4984,320,5006,5002,5756,592,4157,4412,4997,320,5029,4976,4996,320,5018,5014,5788,592,4193,4392,5009,320,5041,4992,5008,320,5030,5026,5780,592,4197,4408,5021,320,5053,5004,5020,320,5042,5038,5820,592,4233,4388,5033,320,5065,5016,5032,320,5054,5050,5812,592,4237,4404,5045,320,5077,5028,5044,320,5066,5062,5844,592,4273,4384,5057,320,4914,5040,5056,320,5078,5074,5852,592,4277,4400,5069,320,4942,5052,5068,320,5090,5086,5232,592,4386,4796,5081,320,5102,5094,5080,528,1,5098,5089,785,5133,4601,5093,320,31,5106,5088,721,5125,4617,5101,320,5118,5114,5244,592,4402,4788,5109,320,5130,5122,5108,528,1,5126,5117,785,4306,5104,5121,320,31,5134,5116,721,4290,5096,5129,320,5161,5142,5884,592,4121,4429,5137,320,5173,5150,5876,592,4125,4445,5145,320,5162,5158,5916,592,4161,4428,5153,320,5185,5136,5152,320,5174,5170,5908,592,4165,4444,5165,320,5197,5144,5164,320,5186,5182,5940,592,4201,4424,5177,320,5209,5160,5176,320,5198,5194,5948,592,4205,4440,5189,320,5221,5172,5188,320,5210,5206,5980,592,4241,4420,5201,320,5233,5184,5200,320,5222,5218,5972,592,4245,4436,5213,320,5245,5196,5212,320,5234,5230,6012,592,4281,4416,5225,320,5082,5208,5224,320,5246,5242,6004,592,4285,4432,5237,320,5110,5220,5236,320,5258,5254,6940,528,4,5277,5249,785,28,5269,5248,721,5274,5266,6964,528,4,5270,5261,785,4506,5257,5265,320,28,5278,5260,721,4518,5253,5273,320,5290,5286,6380,528,3,5309,5281,785,29,5301,5280,721,5306,5298,6400,528,3,5302,5293,785,4542,5289,5297,320,29,5310,5292,721,4530,5285,5305,320,5322,5318,6732,528,9,5341,5313,785,23,5333,5312,721,5338,5330,6756,528,9,5334,5325,785,4554,5321,5329,320,23,5342,5324,721,4566,5317,5337,320,5354,5350,6876,528,18,5373,5345,785,14,5365,5344,721,5370,5362,6900,528,18,5366,5357,785,4590,5353,5361,320,14,5374,5356,721,4578,5349,5369,320,5386,5382,6888,528,1,5405,5377,785,31,5397,5376,721,5402,5394,6912,528,1,5398,5389,785,4650,5385,5393,320,31,5406,5388,721,4642,5381,5401,320,5418,5414,6796,528,12,5437,5409,785,20,5429,5408,721,5434,5426,6820,528,12,5430,5421,785,4658,5417,5425,320,20,5438,5420,721,4670,5413,5433,320,5450,5446,6460,528,10,5469,5441,785,22,5461,5440,721,5466,5458,6480,528,10,5462,5453,785,4694,5449,5457,320,22,5470,5452,721,4682,5445,5465,320,5482,5478,6588,528,13,5501,5473,785,19,5493,5472,721,5498,5490,6612,528,13,5494,5485,785,4706,5481,5489,320,19,5502,5484,721,4718,5477,5497,320,5514,5510,6972,528,2,5533,5505,785,30,5525,5504,721,5530,5522,6996,528,2,5526,5517,785,4742,5513,5521,320,30,5534,5516,721,4730,5509,5529,320,5546,5542,6984,528,30,5565,5537,785,2,5557,5536,721,5562,5554,7008,528,30,5558,5549,785,4802,5545,5553,320,2,5566,5548,721,4810,5541,5561,320,5578,5574,6892,528,6,5597,5569,785,26,5589,5568,721,5594,5586,6916,528,6,5590,5581,785,4830,5577,5585,320,26,5598,5580,721,4818,5573,5593,320,5610,5606,6340,528,11,5629,5601,785,21,5621,5600,721,5626,5618,6360,528,11,5622,5613,785,4842,5609,5617,320,21,5630,5612,721,4854,5605,5625,320,5642,5638,6684,528,15,5661,5633,785,17,5653,5632,721,5658,5650,6708,528,15,5654,5645,785,4878,5641,5649,320,17,5662,5644,721,4866,5637,5657,320,5674,5670,6828,528,29,5693,5665,785,3,5685,5664,721,5690,5682,6852,528,29,5686,5677,785,4890,5673,5681,320,3,5694,5676,721,4902,5669,5689,320,5706,5702,6840,528,28,5725,5697,785,4,5717,5696,721,5722,5714,6864,528,28,5718,5709,785,4978,5705,5713,320,4,5726,5708,721,4970,5701,5721,320,5738,5734,6988,528,23,5757,5729,785,9,5749,5728,721,5754,5746,7012,528,23,5750,5741,785,4986,5737,5745,320,9,5758,5740,721,4998,5733,5753,320,5770,5766,6420,528,25,5789,5761,785,7,5781,5760,721,5786,5778,6440,528,25,5782,5773,785,5022,5769,5777,320,7,5790,5772,721,5010,5765,5785,320,5802,5798,6540,528,21,5821,5793,785,11,5813,5792,721,5818,5810,6564,528,21,5814,5805,785,5046,5801,5809,320,11,5822,5804,721,5034,5797,5817,320,5834,5830,6924,528,24,5853,5825,785,8,5845,5824,721,5850,5842,6948,528,24,5846,5837,785,5058,5833,5841,320,8,5854,5836,721,5070,5829,5849,320,5866,5862,6936,528,27,5885,5857,785,5,5877,5856,721,5882,5874,6960,528,27,5878,5869,785,5146,5865,5873,320,5,5886,5868,721,5138,5861,5881,320,5898,5894,6844,528,20,5917,5889,785,12,5909,5888,721,5914,5906,6868,528,20,5910,5901,785,5166,5897,5905,320,12,5918,5900,721,5154,5893,5913,320,5930,5926,6500,528,7,5949,5921,785,25,5941,5920,721,5946,5938,6520,528,7,5942,5933,785,5178,5929,5937,320,25,5950,5932,721,5190,5925,5945,320,5962,5958,6636,528,8,5981,5953,785,24,5973,5952,721,5978,5970,6660,528,8,5974,5965,785,5214,5961,5969,320,24,5982,5964,721,5202,5957,5977,320,5994,5990,6780,528,14,6013,5985,785,18,6005,5984,721,6010,6002,6804,528,14,6006,5997,785,5238,5993,6001,320,18,6014,5996,721,5226,5989,6009,320,6022,6557,7476,592,6153,6026,6016,464,6141,0,6021,658,6034,6581,7484,592,6173,6038,6028,464,6161,0,6033,658,6046,6605,7653,592,6193,6050,6040,464,6181,0,6045,658,6058,6629,7649,592,6213,6062,6052,464,6201,0,6057,658,6070,6653,7613,592,6233,6074,6064,464,6221,0,6069,658,6082,6677,7609,592,6253,6086,6076,464,6241,0,6081,658,6094,6701,7573,592,6273,6098,6088,464,6261,0,6093,658,6106,6725,7569,592,6293,6110,6100,464,6281,0,6105,658,6118,6749,7533,592,6313,6122,6112,464,6301,0,6117,658,6130,6773,7529,592,6333,6134,6124,464,6321,0,6129,658,6146,6142,7685,592,6797,6024,6137,320,6353,6150,6136,464,6154,0,6145,658,6341,6020,6148,320,6166,6162,7681,592,6821,6036,6157,320,6373,6170,6156,464,6174,0,6165,658,6361,6032,6168,320,6186,6182,7645,592,6845,6048,6177,320,6393,6190,6176,464,6194,0,6185,658,6381,6044,6188,320,6206,6202,7641,592,6869,6060,6197,320,6413,6210,6196,464,6214,0,6205,658,6401,6056,6208,320,6226,6222,7605,592,6893,6072,6217,320,6433,6230,6216,464,6234,0,6225,658,6421,6068,6228,320,6246,6242,7601,592,6917,6084,6237,320,6453,6250,6236,464,6254,0,6245,658,6441,6080,6248,320,6266,6262,7565,592,6941,6096,6257,320,6473,6270,6256,464,6274,0,6265,658,6461,6092,6268,320,6286,6282,7561,592,6965,6108,6277,320,6493,6290,6276,464,6294,0,6285,658,6481,6104,6288,320,6306,6302,7525,592,6989,6120,6297,320,6513,6310,6296,464,6314,0,6305,658,6501,6116,6308,320,6326,6322,7521,592,7013,6132,6317,320,6533,6330,6316,464,6334,0,6325,658,6521,6128,6328,320,6346,6342,7677,592,5602,6152,6337,320,6553,6350,6336,464,6354,0,6345,658,6541,6144,6348,320,6366,6362,7673,592,5614,6172,6357,320,6577,6370,6356,464,6374,0,6365,658,6565,6164,6368,320,6386,6382,7637,592,5282,6192,6377,320,6601,6390,6376,464,6394,0,6385,658,6589,6184,6388,320,6406,6402,7633,592,5294,6212,6397,320,6625,6410,6396,464,6414,0,6405,658,6613,6204,6408,320,6426,6422,7597,592,5762,6232,6417,320,6649,6430,6416,464,6434,0,6425,658,6637,6224,6428,320,6446,6442,7593,592,5774,6252,6437,320,6673,6450,6436,464,6454,0,6445,658,6661,6244,6448,320,6466,6462,7557,592,5442,6272,6457,320,6697,6470,6456,464,6474,0,6465,658,6685,6264,6468,320,6486,6482,7553,592,5454,6292,6477,320,6721,6490,6476,464,6494,0,6485,658,6709,6284,6488,320,6506,6502,7517,592,5922,6312,6497,320,6745,6510,6496,464,6514,0,6505,658,6733,6304,6508,320,6526,6522,7513,592,5934,6332,6517,320,6769,6530,6516,464,6534,0,6525,658,6757,6324,6528,320,6546,6542,7669,592,5794,6352,6537,320,6558,6550,6536,464,6554,0,6545,658,6781,6344,6548,320,6793,6017,6544,320,6570,6566,7665,592,5806,6372,6561,320,6582,6574,6560,464,6578,0,6569,658,6805,6364,6572,320,6817,6029,6568,320,6594,6590,7629,592,5474,6392,6585,320,6606,6598,6584,464,6602,0,6593,658,6829,6384,6596,320,6841,6041,6592,320,6618,6614,7625,592,5486,6412,6609,320,6630,6622,6608,464,6626,0,6617,658,6853,6404,6620,320,6865,6053,6616,320,6642,6638,7589,592,5954,6432,6633,320,6654,6646,6632,464,6650,0,6641,658,6877,6424,6644,320,6889,6065,6640,320,6666,6662,7585,592,5966,6452,6657,320,6678,6670,6656,464,6674,0,6665,658,6901,6444,6668,320,6913,6077,6664,320,6690,6686,7549,592,5634,6472,6681,320,6702,6694,6680,464,6698,0,6689,658,6925,6464,6692,320,6937,6089,6688,320,6714,6710,7545,592,5646,6492,6705,320,6726,6718,6704,464,6722,0,6713,658,6949,6484,6716,320,6961,6101,6712,320,6738,6734,7509,592,5314,6512,6729,320,6750,6742,6728,464,6746,0,6737,658,6973,6504,6740,320,6985,6113,6736,320,6762,6758,7505,592,5326,6532,6753,320,6774,6766,6752,464,6770,0,6761,658,6997,6524,6764,320,7009,6125,6760,320,6786,6782,7661,592,5986,6552,6777,320,6798,6790,6776,464,6794,0,6785,658,4490,6556,6788,320,5410,6140,6784,320,6810,6806,7657,592,5998,6576,6801,320,6822,6814,6800,464,6818,0,6809,658,4498,6580,6812,320,5422,6160,6808,320,6834,6830,7621,592,5666,6600,6825,320,6846,6838,6824,464,6842,0,6833,658,5698,6604,6836,320,5890,6180,6832,320,6858,6854,7617,592,5678,6624,6849,320,6870,6862,6848,464,6866,0,6857,658,5710,6628,6860,320,5902,6200,6856,320,6882,6878,7581,592,5346,6648,6873,320,6894,6886,6872,464,6890,0,6881,658,5378,6652,6884,320,5570,6220,6880,320,6906,6902,7577,592,5358,6672,6897,320,6918,6910,6896,464,6914,0,6905,658,5390,6676,6908,320,5582,6240,6904,320,6930,6926,7541,592,5826,6696,6921,320,6942,6934,6920,464,6938,0,6929,658,5858,6700,6932,320,5250,6260,6928,320,6954,6950,7537,592,5838,6720,6945,320,6966,6958,6944,464,6962,0,6953,658,5870,6724,6956,320,5262,6280,6952,320,6978,6974,7501,592,5506,6744,6969,320,6990,6982,6968,464,6986,0,6977,658,5538,6748,6980,320,5730,6300,6976,320,7002,6998,7497,592,5518,6768,6993,320,7014,7006,6992,464,7010,0,7001,658,5550,6772,7004,320,5742,6320,7000,320,7022,7024,7492,4194264,12,7037,7016,913,7017,7030,7254,4194240,7034,7040,7025,4194264,6,7038,7028,913,7053,7021,7033,320,7029,7046,7150,4194240,7050,7056,7041,4194264,3,7054,7044,913,7069,7036,7049,320,7045,7062,7106,4194240,7066,7072,7057,4194264,2,7070,7060,913,7085,7052,7065,320,7061,7078,7100,4194240,7082,7088,7073,4194264,1,7086,7076,913,7113,7068,7081,320,7077,7092,7096,4194240,7089,1,0,4194246,7090,32898,0,4194246,7074,32906,2147483648,4194246,7110,7116,7058,4194264,5,7114,7104,913,7129,7084,7109,320,7105,7122,7144,4194240,7126,7132,7117,4194264,4,7130,7120,913,7157,7112,7125,320,7121,7136,7140,4194240,7133,2147516416,2147483648,4194246,7134,32907,0,4194246,7118,2147483649,0,4194246,7154,7160,7042,4194264,9,7158,7148,913,7173,7128,7153,320,7149,7166,7210,4194240,7170,7176,7161,4194264,8,7174,7164,913,7189,7156,7169,320,7165,7182,7204,4194240,7186,7192,7177,4194264,7,7190,7180,913,7217,7172,7185,320,7181,7196,7200,4194240,7193,2147516545,2147483648,4194246,7194,32777,2147483648,4194246,7178,138,0,4194246,7214,7220,7162,4194264,11,7218,7208,913,7233,7188,7213,320,7209,7226,7248,4194240,7230,7236,7221,4194264,10,7234,7224,913,7261,7216,7229,320,7225,7240,7244,4194240,7237,136,0,4194246,7238,2147516425,0,4194246,7222,2147483658,0,4194246,7258,7264,7026,4194264,18,7262,7252,913,7277,7232,7257,320,7253,7270,7374,4194240,7274,7280,7265,4194264,15,7278,7268,913,7293,7260,7273,320,7269,7286,7330,4194240,7290,7296,7281,4194264,14,7294,7284,913,7309,7276,7289,320,7285,7302,7324,4194240,7306,7312,7297,4194264,13,7310,7300,913,7337,7292,7305,320,7301,7316,7320,4194240,7313,2147516555,0,4194246,7314,139,2147483648,4194246,7298,32905,2147483648,4194246,7334,7340,7282,4194264,17,7338,7328,913,7353,7308,7333,320,7329,7346,7368,4194240,7350,7356,7341,4194264,16,7354,7344,913,7381,7336,7349,320,7345,7360,7364,4194240,7357,32771,2147483648,4194246,7358,32770,2147483648,4194246,7342,128,2147483648,4194246,7378,7384,7266,4194264,21,7382,7372,913,7397,7352,7377,320,7373,7390,7434,4194240,7394,7400,7385,4194264,20,7398,7388,913,7413,7380,7393,320,7389,7406,7428,4194240,7410,7416,7401,4194264,19,7414,7404,913,7441,7396,7409,320,7405,7420,7424,4194240,7417,32778,0,4194246,7418,2147483658,2147483648,4194246,7402,2147516545,2147483648,4194246,7438,7444,7386,4194264,23,7442,7432,913,7457,7412,7437,320,7433,7450,7472,4194240,7454,7460,7445,4194264,22,7458,7448,913,7705,7440,7453,320,7449,7464,7468,4194240,7461,32896,2147483648,4194246,7462,2147483649,0,4194246,7446,2147516424,2147483648,4194246,6018,7481,7693,592,7493,7477,7482,4194240,6030,7490,7689,592,7494,7489,7485,4194240,7018,7480,7488,320,7502,6994,4286,0,7506,6970,7496,0,7510,6754,7500,0,7514,6730,7504,0,7518,6518,7508,0,7522,6498,7512,0,7526,6318,7516,0,7530,6298,7520,0,7534,6126,7524,0,7538,6114,7528,0,7542,6946,7532,0,7546,6922,7536,0,7550,6706,7540,0,7554,6682,7544,0,7558,6478,7548,0,7562,6458,7552,0,7566,6278,7556,0,7570,6258,7560,0,7574,6102,7564,0,7578,6090,7568,0,7582,6898,7572,0,7586,6874,7576,0,7590,6658,7580,0,7594,6634,7584,0,7598,6438,7588,0,7602,6418,7592,0,7606,6238,7596,0,7610,6218,7600,0,7614,6078,7604,0,7618,6066,7608,0,7622,6850,7612,0,7626,6826,7616,0,7630,6610,7620,0,7634,6586,7624,0,7638,6398,7628,0,7642,6378,7632,0,7646,6198,7636,0,7650,6178,7640,0,7654,6054,7644,0,7658,6042,7648,0,7662,6802,7652,0,7666,6778,7656,0,7670,6562,7660,0,7674,6538,7664,0,7678,6358,7668,0,7682,6338,7672,0,7686,6158,7676,0,7690,6138,7680,0,7694,7486,7684,0,7698,7478,7688,0,4077,7702,7692,0,1,7706,7697,17,4085,7456,7701,320,3917,7716,7712,0,7710,7722,7718,0,7709,7720,7714,0,7717,7721,7713,0,7730,7988,8008,0,7734,7888,7724,0,7738,7876,7728,0,7740,7768,7732,0,7736,7756,7744,0,7742,7765,7748,0,7746,7757,7752,0,7750,7760,7762,0,7741,7749,7764,0,7753,7766,7754,0,7758,7745,7761,0,7737,7784,7852,0,7860,7781,7778,0,7785,7782,7774,0,7786,7773,7777,0,7769,7776,7780,256,7816,7797,7794,0,7801,7798,7790,0,7802,7789,7793,0,7861,7792,7796,256,7832,7813,7810,0,7817,7814,7806,0,7818,7805,7809,0,7788,7808,7812,256,7868,7829,7826,0,7833,7830,7822,0,7834,7821,7825,0,7804,7824,7828,256,7872,7845,7842,0,7849,7846,7838,0,7850,7837,7841,0,7869,7840,7844,256,7770,7873,7858,0,7862,7866,7854,0,7772,7800,7856,256,7870,7874,7857,0,7820,7848,7864,256,7836,7853,7865,0,7733,7884,7880,0,7878,7885,7886,0,7877,7881,7882,0,7729,7904,7892,0,7890,7916,7896,0,7894,7901,7902,0,7906,7897,7898,0,7889,7908,7900,0,7905,7920,7912,0,7910,7925,7918,0,7893,7922,7914,0,7909,7926,7917,0,7928,7913,7921,0,7924,7964,7932,0,7930,7956,7946,0,8,7965,7972,721,8,7973,7984,721,7950,7982,7934,0,7954,7978,7944,0,7958,7970,7948,0,7933,7962,7952,0,256,7966,7957,273,7929,7937,7961,4194240,256,7974,7953,273,7938,7941,7969,4194240,256,7985,7949,273,8,7986,7945,721,7942,7977,7981,4194240,7725,7992,7994,0,7989,7996,7990,0,7993,7998,7997,0,8224,8009,8006,0,3918,8010,8002,0,7726,8001,8005,0,8018,8212,8225,0,8022,8176,8012,0,8026,8164,8016,0,8028,8056,8020,0,8024,8044,8032,0,8030,8053,8036,0,8034,8045,8040,0,8038,8048,8050,0,8029,8037,8052,0,8041,8054,8042,0,8046,8033,8049,0,8025,8072,8140,0,8148,8069,8066,0,8073,8070,8062,0,8074,8061,8065,0,8057,8064,8068,256,8104,8085,8082,0,8089,8086,8078,0,8090,8077,8081,0,8149,8080,8084,256,8120,8101,8098,0,8105,8102,8094,0,8106,8093,8097,0,8076,8096,8100,256,8156,8117,8114,0,8121,8118,8110,0,8122,8109,8113,0,8092,8112,8116,256,8160,8133,8130,0,8137,8134,8126,0,8138,8125,8129,0,8157,8128,8132,256,8058,8161,8146,0,8150,8154,8142,0,8060,8088,8144,256,8158,8162,8145,0,8108,8136,8152,256,8124,8141,8153,0,8021,8168,8170,0,8165,8172,8166,0,8169,8174,8173,0,8017,8184,8180,0,8178,8196,8186,0,8177,8188,8182,0,8185,8208,8192,0,8190,8204,8198,0,8181,8200,8194,0,8197,8209,8206,0,8193,8210,8202,0,8189,8201,8205,0,8013,8216,8218,0,8213,8220,8214,0,8217,8222,8221,0,8000,8014,3622,0,8232,165,449,0,8228,8244,8236,0,8234,8432,8456,0,8246,8248,8460,0,8233,8245,8240,4194240,8241,8452,8252,0,8250,8405,8258,0,8262,8302,8254,0,8264,8294,8256,0,8260,8284,8270,0,8274,8276,8266,4194264,0,8285,8268,849,8269,8282,8288,4194240,8329,8286,8277,0,8265,8273,8281,4194240,8278,8290,8289,0,255,8298,8261,465,0,8349,8293,721,8306,8354,8257,0,8308,8342,8300,0,8304,8332,8314,0,8318,8320,8310,4194264,0,8333,8312,849,8313,8326,8336,4194240,8330,8334,8321,0,8381,8280,8324,128,8309,8317,8325,4194240,8322,8338,8337,0,255,8346,8305,465,8,8350,8341,721,8401,8297,8345,4194240,8358,8406,8301,0,8360,8394,8352,0,8356,8384,8366,0,8370,8372,8362,4194264,0,8385,8364,849,8365,8378,8388,4194240,8382,8386,8373,0,8433,8328,8376,128,8361,8369,8377,4194240,8374,8390,8389,0,255,8398,8357,465,16,8402,8393,721,8453,8348,8397,4194240,8410,8253,8353,0,8412,8446,8404,0,8408,8436,8418,0,8422,8424,8414,4194264,0,8437,8416,849,8417,8430,8440,4194240,8434,8438,8425,0,8237,8380,8428,128,8413,8421,8429,4194240,8426,8442,8441,0,255,8450,8409,465,24,8454,8445,721,8249,8400,8449,4194240,8238,8461,8462,0,8242,8457,8458,0,6,8468,1,0,8465,1954047348,8472,4194242,8470,8473,8476,0,8474,8478,8477,0
};

int main () {
  Net net;
  net.nodes = reinterpret_cast<uint32_t*>(malloc(sizeof(u32) * 200000000));
  net.redex = reinterpret_cast<uint32_t*>(malloc(sizeof(u32) * 10000000));
  net.freed = reinterpret_cast<uint32_t*>(malloc(sizeof(u32) * 10000000));

  net.nodes_len = 0;
  net.redex_len = 0;
  net.freed_len = 0;

  for (u32 i = 0; i < sizeof(nodes) / sizeof(u32); ++i) {
    net.nodes[i] = nodes[i];
    net.nodes_len += 1;
  }

  find_redexes(&net);
  Stats stats = reduce(&net);

  // Must output 44067986
  printf("rewrites: %d\n", stats.rewrites);
  printf("loops: %d\n", stats.loops);
}
#endif
int main(){
  printf("%u bits for label, %u bits for node type, %d bits for isnum bits\n", config_u32array::num_of_bits_4_label, num_of_bits_4_node_typ, INFO);
  return 0;
}
