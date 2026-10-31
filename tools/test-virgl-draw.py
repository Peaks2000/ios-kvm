#!/usr/bin/env python3
"""Offline checks of draw-probe framing and rejection, without a renderer or VM."""
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
import virgl_draw as draw


class Connection:
    def __init__(self,data):
        self.data=data
        self.commands=[]
    def sendall(self,data):
        length,command=struct.unpack('<II',data[:8])
        assert len(data)==8+4*length
        self.commands.append(command)
    def recv(self,length):
        length=min(length,7)  # Deliberately fragment every pixel reply.
        result,self.data=self.data[:length],self.data[length:]
        return result


class Probe(unittest.TestCase):
    def check(self,data):
        connection=Connection(data)
        with patch.object(draw,'build_draw_encoder',return_value=Path('/unused')), \
             patch.object(draw.subprocess,'check_output',return_value=b'\0'*4):
            result=draw.probe_draw(connection)
        return result,connection
    def test_real_frame_expectations(self):
        self.assertEqual(draw.expected_pixel(0),bytes((0,0,0,255)))
        self.assertEqual(draw.expected_pixel(1),bytes((32,65,95,255)))
        self.assertEqual(draw.expected_pixel(2),bytes((26,58,74,255)))
    def test_fragmented_complete_frames_pass_and_commands_are_framed(self):
        result,connection=self.check(draw.expected_pixel(0)*64+draw.expected_pixel(1)*64)
        self.assertEqual(connection.commands,[2,2,6,4,6,4])
        self.assertEqual(result,{'ok':True,'frames':2,'pixels':128,'alpha_blend':True})
    def test_stale_second_frame_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'mismatch on frame 1'):
            self.check(draw.expected_pixel(0)*128)
    def test_truncated_reply_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'disconnected'):
            self.check(draw.expected_pixel(0)*63)
    def test_one_unit_tolerance_checks_all_channels_and_pixels(self):
        pixel=bytes((0,128,255,255))
        self.assertTrue(draw.pixels_match(bytes((1,127,254,255))*64,pixel))
        self.assertFalse(draw.pixels_match(pixel*63+bytes((0,126,255,255)),pixel))
        self.assertFalse(draw.pixels_match(b'',pixel))
        self.assertFalse(draw.pixels_match(b'\0',pixel))


if __name__=='__main__': unittest.main()
