"""GDB decoders for Havel runtime Values.

Load from a GDB session started in the repo root:

    (gdb) source tools/gdb/havel.py

Tell the module where the VM lives so host-function references resolve to their
registered names instead of raw indices:

    (gdb) havel-vm this

These are explicit commands, not registered gdb.pretty_printers. On GDB 18.1 a
catch-all printer subclass degraded ordinary `print` output for ints, pointers
and unrelated structs, and lookup-function printers rendered empty values. See
tools/gdb/README.md. Commands decode on demand and leave `print` untouched.

Decoding only ever reads the raw `bits_` payload and plain data members. It
never calls into the inferior, so a decode cannot deadlock the target or
re-enter GDB halfway through printing. Resolving a string's text needs the VM's
chunk tables, which is an inferior call, so that stays behind the opt-in
`havel-str` command instead of happening implicitly.

The encoding below mirrors src/havel-lang/core/Value.hpp. If Value changes its
NaN-boxing layout, change this file in the same commit.
"""

import gdb
import struct

# --- NaN-boxing layout (mirrors src/havel-lang/core/Value.hpp) --------------

_QNAN = 0x7FF8000000000000
_TAG_MASK = 0x0007000000000000
_PAYLOAD_MASK = 0x0000FFFFFFFFFFFF
_INT48_SIGN_BIT = 0x0000800000000000
_EXT_TAG_MASK = 0x0000F80000000000
_EXT_TAG_SHIFT = 43
_EXT_PAYLOAD_MASK = 0x000007FFFFFFFFFF

# Primary tags (bits 48-50) each get bespoke rendering in _format_bits, so
# there is no name table for them: 0x0 double (handled as an untagged double),
# 0x1 int, 0x2 bool, 0x3 nil, 0x4 ptr, 0x5 string-id, 0x6 object-id,
# 0x7 extended.
#
# Extended sub-tags (bits 43-47) fall back to this table for the ones with no
# structured payload layout of their own.
_EXT_TAG_NAMES = {
    0x00: "closure",
    0x01: "array",
    0x02: "set",
    0x03: "range",
    0x04: "channel",
    0x05: "coroutine",
    0x06: "enum",
    0x07: "iterator",
    0x08: "host-fn",
    0x09: "lazy-pipeline",
    0x0A: "error",
    0x0B: "function-obj",
    0x0C: "string-val",
    0x0D: "thread",
    0x0E: "interval",
    0x0F: "timeout",
    0x10: "tail-call",
    0x11: "regex-val",
    0x12: "bound-method",
    0x13: "waitgroup",
    0x14: "string-cursor",
    0x15: "pending",
}

# The VM the printers read host-function names through. A GDB expression, not a
# cached gdb.Value, so a new frame at a different address is picked up.
_vm_expr = None


def _sign_extend48(payload):
    if payload & _INT48_SIGN_BIT:
        return payload | 0xFFFF000000000000
    return payload


def _as_signed(value, bits=64):
    if value & (1 << (bits - 1)):
        return value - (1 << bits)
    return value


def _host_fn_name(index):
    """Look up a host-function name. Field reads only, no inferior calls."""
    if _vm_expr is None or index < 0:
        return None
    # GDB's expression evaluator accepts `->` on both a VM object and a VM
    # pointer, so one spelling covers every form `havel-vm` accepts.
    try:
        entry = gdb.parse_and_eval(
            "({0}->host_function_names_[{1}])".format(_vm_expr, index)
        )
        text = str(entry)
    except gdb.error:
        return None
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        return text[1:-1]
    return None


def _format_double(bits):
    value = struct.unpack("<d", struct.pack("<Q", bits & 0xFFFFFFFFFFFFFFFF))[0]
    if value != value:
        return "nan"
    if value == float("inf"):
        return "inf"
    if value == float("-inf"):
        return "-inf"
    if value == int(value) and abs(value) < 1e16:
        return "%.1f" % value
    return repr(value)


def _format_bits(bits):
    """Render one Value's raw payload. No inferior calls."""
    if (bits & _QNAN) != _QNAN:
        return _format_double(bits)

    tag = (bits & _TAG_MASK) >> 48
    payload = bits & _PAYLOAD_MASK

    if tag == 0x1:  # INT48
        return str(_as_signed(_sign_extend48(payload)))
    if tag == 0x2:  # BOOL
        if payload == 0:
            return "false"
        if payload == 1:
            return "true"
        if payload == 2:
            return "<default-array>"
        return "<bool 0x%x>" % payload
    if tag == 0x3:  # NULL_
        return "nil"
    if tag == 0x4:  # PTR
        return "0x%x" % _sign_extend48(payload)
    if tag == 0x5:  # STRING_ID (heap string table)
        return "<string-id %d>" % payload
    if tag == 0x6:  # OBJECT_ID
        return "<object %d>" % payload

    # tag == 0x7, EXTENDED: sub-tag in bits 43-47, payload in bits 0-42.
    ext = (bits & _EXT_TAG_MASK) >> _EXT_TAG_SHIFT
    body = bits & _EXT_PAYLOAD_MASK
    name = _EXT_TAG_NAMES.get(ext)
    if name is None:
        return "<ext 0x%x %d>" % (ext, body)

    if ext == 0x06:  # ENUM_ID: typeId in bits 32-42, index in bits 0-31
        return "%s<%d:%d>" % (name, (body >> 32) & 0x7FF, body & 0xFFFFFFFF)
    if ext in (0x0C, 0x11):  # STRING_VAL_ID, REGEX_VAL_ID: chunk 31-42, index 0-30
        return "%s<%d:%d>" % (name, (body >> 31) & 0xFFF, body & 0x7FFFFFFF)
    if ext == 0x08:  # HOST_FUNC_ID
        registered = _host_fn_name(body & 0xFFFFFFFF)
        if registered is not None:
            return "fn %s [%d]" % (registered, body & 0xFFFFFFFF)
        return "<host-fn %d>" % body
    return "<%s %d>" % (name, body)


def _bits_of(value):
    """Read the private NaN-box payload out of a havel::core::Value."""
    for field in ("bits_", "_bits_"):
        try:
            return int(value[field])
        except (gdb.error, KeyError, ValueError):
            continue
    raise gdb.GdbError("havel::Value has no readable bits_ field")


def _string_of(expr):
    """Read a std::string member through c_str(), so no quoting to strip."""
    try:
        return gdb.parse_and_eval("(%s).c_str()" % expr).string()
    except (gdb.error, AttributeError):
        try:
            return str(gdb.parse_and_eval(expr)).strip('"')
        except gdb.error:
            return ""


def _unwrap_type(type_):
    """Strip typedefs, references, and pointers, so "const VM *" compares
        equal to VM.

        strip_typedefs() alone leaves reference codes intact in this GDB, which
        would make a VM reference look like a different type than a VM value.
    Pointers matter too: inside any VM method `this` is a `const VM *`, which
        is how a VM is normally reached from a breakpoint. The const qualifier has
        to go as well, since gdb.Type.code cannot distinguish it.
    """
    while True:
        code = type_.code
        if code in (gdb.TYPE_CODE_REF, gdb.TYPE_CODE_RVALUE_REF, gdb.TYPE_CODE_PTR):
            type_ = type_.target()
        elif code == gdb.TYPE_CODE_TYPEDEF:
            type_ = type_.strip_typedefs()
        else:
            return type_.unqualified() if hasattr(type_, "unqualified") else type_


def _dig(value, path):
    """Walk a dotted field path. field["a.b"] is not supported by GDB."""
    for part in path.split("."):
        value = value[part]
    return value


# std::optional keeps its state in private members whose nesting differs
# between libstdc++ versions, and GDB can neither call has_value() ("may be
# inlined") nor find operator* in this toolchain. Probe both layouts and use
# whichever reports a usable engaged flag. Each entry is
# (path to the engaged flag, path to the held value).
_OPTIONAL_LAYOUTS = (
    ("_M_engaged", "_M_payload"),
    ("_M_payload._M_engaged", "_M_payload._M_payload._M_value"),
)


def _optional_held(optional_value):
    """Return the value inside a std::optional gdb.Value, else None."""
    for engaged_path, value_path in _OPTIONAL_LAYOUTS:
        try:
            engaged = _dig(optional_value, engaged_path)
        except (gdb.error, KeyError):
            continue
        try:
            if not bool(engaged):
                continue
            return _dig(optional_value, value_path)
        except (gdb.error, KeyError, TypeError, ValueError):
            continue
    return None


def _format_location(member_expr):
    """Render an optional<SourceLocation> member as 'at file:line:column'."""
    try:
        held = _optional_held(gdb.parse_and_eval(member_expr))
    except gdb.error as exc:
        return "at <unreadable location: %s>" % exc
    if held is None:
        return "at <no location>"
    try:
        return "at %s:%d:%d" % (
            _string_of_value(held, "filename"),
            int(held["line"]),
            int(held["column"]),
        )
    except (gdb.error, KeyError, TypeError, ValueError) as exc:
        return "at <unreadable location: %s>" % exc


def _string_of_value(owner, member):
    """Read a std::string field out of an already-resolved gdb.Value."""
    try:
        return owner[member]["c_str()"].string()
    except (gdb.error, KeyError, AttributeError):
        try:
            return str(owner[member]).strip('"')
        except (gdb.error, KeyError):
            return ""


def _arity_text(hostfn_expr):
    """Arity of a HostFunctionInfo, or 'any' when the optional is empty."""
    try:
        held = _optional_held(gdb.parse_and_eval("(%s).arity" % hostfn_expr))
    except gdb.error:
        return "any"
    if held is None:
        return "any"
    try:
        return str(int(held))
    except (TypeError, ValueError):
        return str(held)


class _DecodeCommand(gdb.Command):
    """Base for the havel-* commands: resolve one expression, decode, print.

    Commands rather than pretty-printers on purpose. Under GDB 18.1 a
    pretty-printer appended to gdb.pretty_printers is consulted for *every*
    type and no per-type name matching takes effect, so a catch-all printer
    must also render everything it does not recognise. Reproducing GDB's own
    rendering for those types is not possible from inside a printer (str(value)
    re-enters it; format_string(raw=True) exposes libstdc++ internals), so a
    catch-all would degrade ordinary `print` output for ints, pointers and
    unrelated structs. Commands decode on demand and leave GDB's printing
    untouched.
    """

    KIND = ""

    def __init__(self):
        super(_DecodeCommand, self).__init__(self.KIND, gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        argument = argument.strip()
        if not argument:
            raise gdb.GdbError("usage: %s <expression>" % self.KIND)
        try:
            gdb.parse_and_eval("(%s)" % argument)
        except gdb.error as exc:
            raise gdb.GdbError("cannot evaluate %r: %s" % (argument, exc))
        for line in self.decode(argument):
            print(line)


class HavelValueCommand(_DecodeCommand):
    """Decode a havel Value: havel-value <expression>"""

    KIND = "havel-value"

    def decode(self, expr):
        try:
            return [_format_bits(_bits_of(gdb.parse_and_eval("(%s)" % expr)))]
        except gdb.GdbError as exc:
            return ["<unreadable Value: %s>" % exc]


class HavelVectorCommand(_DecodeCommand):
    """Decode every element of a std::vector<Value>: havel-vector <expression>"""

    KIND = "havel-vector"

    def decode(self, expr):
        try:
            count = int(gdb.parse_and_eval("(%s).size()" % expr))
        except gdb.error as exc:
            return ["<not a sized container: %s>" % exc]
        if count == 0:
            return ["<empty vector>"]
        items = []
        for index in range(count):
            try:
                element = gdb.parse_and_eval("(%s)[%d]" % (expr, index))
                items.append("[%d] %s" % (index, _format_bits(_bits_of(element))))
            except (gdb.GdbError, gdb.error) as exc:
                items.append("[%d] <unreadable: %s>" % (index, exc))
        return items


class HavelInstructionCommand(_DecodeCommand):
    """Decode an Instruction: havel-instruction <expression>"""

    KIND = "havel-instruction"

    def decode(self, expr):
        try:
            opcode = str(gdb.parse_and_eval("(%s).opcode" % expr)).rsplit("::", 1)[-1]
        except gdb.error:
            return ["<unreadable Instruction>"]
        lines = ["op=%s" % opcode]
        lines.append(_format_location("(%s).location" % expr))
        try:
            count = int(gdb.parse_and_eval("(%s).operands.size()" % expr))
        except gdb.error as exc:
            lines.append("  operands <unreadable: %s>" % exc)
            return lines
        if count == 0:
            lines.append("  <no operands>")
        for index in range(count):
            try:
                element = gdb.parse_and_eval("(%s).operands[%d]" % (expr, index))
                lines.append(
                    "  operand[%d] %s" % (index, _format_bits(_bits_of(element)))
                )
            except (gdb.GdbError, gdb.error) as exc:
                lines.append("  operand[%d] <unreadable: %s>" % (index, exc))
        return lines


class HavelHostFunctionCommand(_DecodeCommand):
    """Decode a HostFunctionInfo: havel-hostfn <expression>"""

    KIND = "havel-hostfn"

    def decode(self, expr):
        def string_of(member):
            return _string_of("(%s).%s" % (expr, member))

        module = string_of("module")
        rows = [
            "%s [%s] arity=%s"
            % (string_of("name"), module if module else "-", _arity_text(expr))
        ]
        for member in ("index", "namespace_prefix", "callable", "bound_as_global"):
            try:
                value = gdb.parse_and_eval("(%s).%s" % (expr, member))
            except gdb.error as exc:
                rows.append("  %s <unreadable: %s>" % (member, exc))
                continue
            text = str(value)
            if member == "namespace_prefix":
                text = text.strip('"')
            rows.append("  %s = %s" % (member, text))
        return rows


class HavelVMCommand(gdb.Command):
    """Point name resolution at the VM: havel-vm [gdb-expression]."""

    def __init__(self):
        super(HavelVMCommand, self).__init__("havel-vm", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        global _vm_expr
        argument = argument.strip()
        if not argument:
            if _vm_expr is None:
                raise gdb.GdbError("no VM expression set; usage: havel-vm vm")
            print("havel-vm: %s" % _vm_expr)
            return
        try:
            parsed = gdb.parse_and_eval("(%s)" % argument)
        except gdb.error as exc:
            raise gdb.GdbError("cannot evaluate %r: %s" % (argument, exc))
        type_name = str(_unwrap_type(parsed.type))
        try:
            expected = str(_unwrap_type(gdb.lookup_type("havel::compiler::VM")))
        except gdb.error:
            expected = "havel::compiler::VM"
        if type_name != expected:
            raise gdb.GdbError(
                "%r is a %s, not a VM (expected %s)" % (argument, type_name, expected)
            )
        _vm_expr = argument
        print("havel-vm: %s (%s)" % (_vm_expr, type_name))


class HavelStringCommand(gdb.Command):
    """Print a string Value's text through the VM: havel-str <expression>."""

    def __init__(self):
        super(HavelStringCommand, self).__init__("havel-str", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        argument = argument.strip()
        if not argument:
            raise gdb.GdbError("usage: havel-str <expression>")
        if _vm_expr is None:
            raise gdb.GdbError("no VM expression set; run 'havel-vm vm' first")
        try:
            resolved = gdb.parse_and_eval(
                "({0}).resolveStringKey({1})".format(_vm_expr, argument)
            )
        except gdb.error as exc:
            raise gdb.GdbError("resolveStringKey failed: %s" % exc)
        print(str(resolved).strip('"'))


print(
    "havel.py: run 'havel-value', 'havel-vector', 'havel-instruction', "
    "'havel-hostfn', 'havel-vm', 'havel-str'"
)
HavelValueCommand()
HavelVectorCommand()
HavelInstructionCommand()
HavelHostFunctionCommand()
HavelVMCommand()
HavelStringCommand()
