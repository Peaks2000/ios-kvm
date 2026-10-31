"""Small actual VirGL shader/render check shared by selection and the RAM-transfer test."""
import json
import os
from pathlib import Path
import platform
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def build_draw_encoder():
    source = ROOT / '.local/virglrenderer'
    build = ROOT / '.local/virgl-build'
    binary = ROOT / '.local/virgl-draw-commands'
    receipt = binary.with_suffix('.json')
    dependencies = [ROOT/'tools/virgl-draw-commands.c', Path(__file__),
                    source/'tests/testvirgl_encode.c',source/'tests/testvirgl_encode.h',
                    source/'tests/testvirgl.h',source/'src/virgl_protocol.h',
                    build/'config.h',build/'src/virgl-version.h',build/'src/gallium/u_format_gen.h',
                    build/'src/gallium/libgallium.a',build/'src/mesa/libmesa.a']
    stamp = {'machine':platform.machine(),
             'inputs':[[str(path),path.stat().st_mtime_ns,path.stat().st_size] for path in dependencies]}
    try:
        if binary.is_file() and json.loads(receipt.read_text()) == stamp:
            return binary
    except (OSError,ValueError):
        pass
    includes = [build,build/'src',build/'src/gallium',source/'src',source/'src/mesa',
                source/'src/mesa/compat',source/'src/mesa/pipe',source/'src/mesa/util',
                source/'src/gallium/include',source/'src/gallium/auxiliary',source/'tests']
    flags = [value for path in includes for value in ('-I',str(path))]
    temporary = binary.with_name(f'{binary.name}.{os.getpid()}.tmp')
    try:
        subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections','-imacros',str(build/'config.h'),
                        '-DHAVE_CONFIG_H',*flags,str(ROOT/'tools/virgl-draw-commands.c'),
                        str(source/'tests/testvirgl_encode.c'),str(build/'src/gallium/libgallium.a'),
                        str(build/'src/mesa/libmesa.a'),'-Wl,--gc-sections','-lm','-o',str(temporary)],check=True,timeout=30)
        temporary.replace(binary)
        temporary_receipt = receipt.with_name(f'{receipt.name}.{os.getpid()}.tmp')
        temporary_receipt.write_text(json.dumps(stamp)+'\n')
        temporary_receipt.replace(receipt)
    finally:
        temporary.unlink(missing_ok=True)
    return binary


def expected_pixel(frame):
    color = [(frame*13)%256,(frame*29)%256,(frame*37)%256,255]
    if frame & 1:
        color[:3] = [(color[c]*128+(51,102,153)[c]*127+127)//255 for c in range(3)]
    return bytes(color)


def pixels_match(actual,pixel,tolerance=1):
    if not actual or len(actual)%4 or len(pixel)!=4 or tolerance<0:
        return False
    for channel in range(4):
        table = bytes(int(abs(value-pixel[channel])>tolerance) for value in range(256))
        if actual[channel::4].translate(table).count(b'\x01'):
            return False
    return True


def probe_draw(connection):
    encoder = build_draw_encoder()
    width,height = 8,8
    def send(command,data=b''):
        connection.sendall(struct.pack('<II',len(data)//4,command)+data)
    def read(length):
        output=bytearray()
        while len(output)<length:
            data=connection.recv(length-len(output))
            if not data:
                raise RuntimeError('VirGL disconnected during shader rendering')
            output.extend(data)
        return bytes(output)
    send(2,struct.pack('<10I',1,2,67,10,width,height,1,1,0,0))
    send(2,struct.pack('<10I',2,0,64,16,96,1,1,1,0,0))
    for frame in range(2):
        commands=subprocess.check_output([str(encoder),str(frame),str(width),str(height)],timeout=5)
        send(6,commands)
        send(4,struct.pack('<11I',1,0,width*4,0,0,0,0,width,height,1,width*height*4))
        actual=read(width*height*4)
        if not pixels_match(actual,expected_pixel(frame)):
            raise RuntimeError(f'VirGL shader/blend pixel mismatch on frame {frame}')
    return {'ok':True,'frames':2,'pixels':width*height*2,'alpha_blend':True}
