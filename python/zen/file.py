'''
File I/O control
'''

import os
import shutil
import tempfile

from .py import *
from .kwd import G


iopath = None

def setIOPath(path):
    shutil.rmtree(path, ignore_errors=True)
    os.mkdir(path)
    global iopath
    iopath = path


@defNodeClass
class MakeString(INode):
    z_params = [('string', 'value', '')]
    z_outputs = ['value']
    z_categories = 'imexport'

    def apply(self):
        value = self.get_param('value')
        self.set_output('value', value)


@defNodeClass
class ExportPath(INode):
    z_params = [('string', 'name', 'out.zpm')]
    z_outputs = ['path']
    z_categories = 'imexport'

    def apply(self):
        name = self.get_param('name')
        assert iopath is not None, 'please zen.setIOPath first'
        dirpath = os.path.join(iopath, '{:06d}'.format(G.frameid))
        if not os.path.isdir(dirpath):
            os.mkdir(dirpath)
        path = os.path.join(dirpath, name)
        self.set_output('path', path)


@defNodeClass
class ExportShader(INode):
    z_params = [('string', 'type', 'points')]
    z_inputs = ['primPath', 'fragShaderPath', 'vertShaderPath']
    z_categories = 'imexport'

    def apply(self):
        type = self.get_param('type')
        primPath = self.get_input('primPath')
        vertShaderPath = self.get_input('vertShaderPath')
        fragShaderPath = self.get_input('fragShaderPath')
        shutil.copy(vertShaderPath, primPath + '.' + type + '.vert')
        shutil.copy(fragShaderPath, primPath + '.' + type + '.frag')


__all__ = ['setIOPath']
