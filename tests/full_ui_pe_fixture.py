"""Synthetic PE/RCC builders adapted from historical void_auto_apply_test.py; never executed."""
import struct
A=None  # assigned the pinned resource engine by the caller

def rcc(entries, version=2):
    """Real RCC array layout; Qt hash-sorted children, nested directories supported."""
    directories = {}
    for full, content in entries.items():
        parts = full.split('/')
        node = directories
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = content
    nodes = [{'name': '', 'value': directories}]
    data, names, name_offsets = bytearray(), bytearray(), {}
    index = 0
    while index < len(nodes):
        item = nodes[index]; value = item['value']
        if isinstance(value, dict):
            item['first'], item['count'] = len(nodes), len(value)
            for key in sorted(value, key=lambda x: (A.qhash(x), x)):
                nodes.append({'name': key, 'value': value[key]})
        else:
            body, flags = value
            item['offset'], item['flags'] = len(data), flags
            data.extend(A.record(body, flags))
            data.extend(b'\0' * (A.align(len(data), 4) - len(data)))
        if item['name'] and item['name'] not in name_offsets:
            text = item['name']; name_offsets[text] = len(names)
            names.extend(struct.pack('>HI', len(text.encode('utf-16-be')) // 2, A.qhash(text)))
            names.extend(text.encode('utf-16-be'))
        index += 1
    stride = 14 if version == 1 else 22
    tree = bytearray(len(nodes) * stride)
    for i, item in enumerate(nodes):
        at = i * stride
        A.put(tree, at, '>I', name_offsets.get(item['name'], 0))
        if isinstance(item['value'], dict):
            A.put(tree, at + 4, '>H', 2)
            A.put(tree, at + 6, '>I', item['count'])
            A.put(tree, at + 10, '>I', item['first'])
        else:
            A.put(tree, at + 4, '>H', item['flags'])
            A.put(tree, at + 10, '>I', item['offset'])
            if version >= 2:
                A.put(tree, at + 14, '>Q', 1700000000000)
    names_at = 24 + len(data)
    tree_at = names_at + len(names)
    return b'qres' + struct.pack('>IIII', version, tree_at, 24, names_at) + b'\0' * 4 + data + names + tree

def unit(owner, version=77, extra=b'', qt=0x050f04):
    b = bytearray(252)
    b[:8] = A.CACHE_MAGIC
    A.put(b, 8, '<I', version); A.put(b, 12, '<I', qt)
    A.put(b, 112, '<I', 1); A.put(b, 116, '<I', 248)
    A.put(b, 236, '<I', 0); A.put(b, 248, '<I', 252)
    encoded = owner.encode('utf-16-le')
    header = struct.pack('<I', len(encoded) // 2) if qt >> 16 == 6 else struct.pack('<iIIIII', -1, len(encoded) // 2, 0, 24, 24, 0)
    b.extend(header + encoded + b'\0\0' + extra)
    A.put(b, 24, '<I', len(b))
    return bytes(b)

def pe(blob, plus=True, registration=False, hide_header=False, extra=b'', certificate=False):
    """Synthetic PE with correct mapped RVAs; never executed as program code."""
    machine, magic, opt_size = (0x8664, 0x20b, 240) if plus else (0x14c, 0x10b, 224)
    base = 0x140000000 if plus else 0x400000
    data = bytearray(blob + extra)
    data_raw, data_rva = (0x600, 0x2000) if registration else (0x400, 0x1000)
    tree_rva = data_rva + A.be32(blob, 8)
    names_rva = data_rva + A.be32(blob, 16)
    payload_rva = data_rva + A.be32(blob, 12)
    if hide_header:
        data[:4] = b'XXXX'
    instructions = b''
    if registration:
        off = A.align(len(data), 8)
        data.extend(b'\0' * (off - len(data)) + b'\0' * 40)
        width = 8 if plus else 4
        lookup = len(data); data.extend(b'\0' * (2 * width))
        iat = len(data); data.extend(b'\0' * (2 * width))
        library = len(data); data.extend(b'Qt5Core.dll\0')
        symbol = len(data); data.extend(b'\0\0?qRegisterResourceData@@YA_NHPEBE00@Z\0')
        A.put(data, off, '<I', data_rva + lookup)
        A.put(data, off + 12, '<I', data_rva + library)
        A.put(data, off + 16, '<I', data_rva + iat)
        for place in (lookup, iat):
            A.put(data, place, '<Q' if plus else '<I', data_rva + symbol)
        if plus:
            code = bytearray(b'\xb9' + struct.pack('<I', A.be32(blob, 4)))
            for op, target in [(b'\x48\x8d\x15', tree_rva), (b'\x4c\x8d\x05', names_rva), (b'\x4c\x8d\x0d', payload_rva)]:
                code.extend(op + struct.pack('<i', target - (0x1000 + len(code) + 7)))
            code.extend(b'\xff\x15' + struct.pack('<i', data_rva + iat - (0x1000 + len(code) + 6)))
        else:
            code = bytearray()
            for target in (payload_rva, names_rva, tree_rva):
                code.extend(b'\x68' + struct.pack('<I', base + target))
            code.extend(b'\x6a' + bytes([A.be32(blob, 4)]))
            code.extend(b'\xff\x15' + struct.pack('<I', base + data_rva + iat))
        instructions = bytes(code)
    b = bytearray(data_raw + A.align(len(data), 512))
    b[:2] = b'MZ'; A.put(b, 0x3c, '<I', 0x80)
    b[0x80:0x84] = b'PE\0\0'
    A.put(b, 0x84, '<H', machine); A.put(b, 0x86, '<H', 2 if registration else 1)
    A.put(b, 0x94, '<H', opt_size)
    opt = 0x98
    A.put(b, opt, '<H', magic)
    A.put(b, opt + (24 if plus else 28), '<Q' if plus else '<I', base)
    A.put(b, opt + 32, '<I', 4096); A.put(b, opt + 36, '<I', 512)
    A.put(b, opt + 8, '<I', A.align(len(data), 512))
    A.put(b, opt + 56, '<I', A.align(data_rva + len(data), 4096))
    A.put(b, opt + 60, '<I', 0x400)
    directory = opt + (112 if plus else 96)
    A.put(b, directory - 4, '<I', 16)
    table = opt + opt_size
    if registration:
        b[table:table + 8] = b'.text\0\0\0'
        for offset, value in [(8, 512), (12, 0x1000), (16, 512), (20, 0x400), (36, 0x60000020)]:
            A.put(b, table + offset, '<I', value)
        b[0x400:0x400 + len(instructions)] = instructions
        A.put(b, directory + 8, '<I', data_rva + off); A.put(b, directory + 12, '<I', 40)
        table += 40
    b[table:table + 8] = b'.rdata\0\0'
    for offset, value in [(8, len(data)), (12, data_rva), (16, A.align(len(data), 512)), (20, data_raw), (36, 0x40000040)]:
        A.put(b, table + offset, '<I', value)
    b[data_raw:data_raw + len(data)] = data
    if certificate:
        at = A.align(len(b), 8); b.extend(b'\0' * (at - len(b)))
        cert = struct.pack('<IHH', 16, 0x200, 2) + b'CERTDATA'
        b.extend(cert)
        A.put(b, directory + 32, '<I', at); A.put(b, directory + 36, '<I', len(cert))
    return bytes(b)
