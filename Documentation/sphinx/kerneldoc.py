# coding=utf-8
#
# Copyright © 2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# Authors:
#    Jani Nikula <jani.nikula@intel.com>
#
# Please make sure this works on both python2 and python3.
#

import codecs
import os
import subprocess
import sys
import re
import glob

from docutils import nodes, statemachine
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives, Directive

#
# AutodocReporter is only good up to Sphinx 1.7
#
import sphinx
from security import safe_command

Use_SSI = sphinx.__version__[:3] >= '1.7'
if Use_SSI:
    from sphinx.util.docutils import switch_source_input
else:
    from sphinx.ext.autodoc import AutodocReporter

import kernellog

__version__  = '1.0'

class KernelDocDirective(Directive):
    """Extract kernel-doc comments from the specified file"""
    required_argument = 1
    optional_arguments = 4
    option_spec = {
        'doc': directives.unchanged_required,
        'functions': directives.unchanged_required,
        'export': directives.unchanged,
        'internal': directives.unchanged,
    }
    has_content = False

    def run(self):
        env = self.state.document.settings.env
        cmd = [env.config.kerneldoc_bin, '-rst', '-enable-lineno']

        filename = env.config.kerneldoc_srctree + '/' + self.arguments[0]
        export_file_patterns = []

        # Tell sphinx of the dependency
        env.note_dependency(os.path.abspath(filename))

        tab_width = self.options.get('tab-width', self.state.document.settings.tab_width)

        # FIXME: make this nicer and more robust against errors
        if 'export' in self.options:
            cmd += ['-export']
            export_file_patterns = str(self.options.get('export')).split()
        elif 'internal' in self.options:
            cmd += ['-internal']
            export_file_patterns = str(self.options.get('internal')).split()
        elif 'doc' in self.options:
            cmd += ['-function', str(self.options.get('doc'))]
        elif 'functions' in self.options:
            for f in str(self.options.get('functions')).split():
                cmd += ['-function', f]

        for pattern in export_file_patterns:
            for f in glob.glob(env.config.kerneldoc_srctree + '/' + pattern):
                env.note_dependency(os.path.abspath(f))
                cmd += ['-export-file', f]

        cmd += [filename]

        try:
            kernellog.verbose(env.app,
                              'calling kernel-doc \'%s\'' % (" ".join(cmd)))

            p = safe_command.run(subprocess.Popen, cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            out, err = p.communicate()

            out, err = codecs.decode(out, 'utf-8'), codecs.decode(err, 'utf-8')

            if p.returncode != 0:
                sys.stderr.write(err)

                kernellog.warn(env.app,
                               'kernel-doc \'%s\' failed with return code %d' % (" ".join(cmd), p.returncode))
                return [nodes.error(None, nodes.paragraph(text = "kernel-doc missing"))]
            elif env.config.kerneldoc_verbosity > 0:
                sys.stderr.write(err)

            lines = statemachine.string2lines(out, tab_width, convert_whitespace=True)
            result = ViewList()

            lineoffset = 0;
            line_regex = re.compile("^#define LINENO ([0-9]+)$")
            for line in lines:
                match = line_regex.search(line)
                if match:
                    # sphinx counts lines from 0
                    lineoffset = int(match.group(1)) - 1
                    # we must eat our comments since the upset the markup
                else:
                    result.append(line, filename, lineoffset)
                    lineoffset += 1

            node = nodes.section()
            self.do_parse(result, node)

            return node.children

        except Exception as e:  # pylint: disable=W0703
            kernellog.warn(env.app, 'kernel-doc \'%s\' processing failed with: %s' %
                           (" ".join(cmd), str(e)))
            return [nodes.error(None, nodes.paragraph(text = "kernel-doc missing"))]

    def do_parse(self, result, node):
        if Use_SSI:
            with switch_source_input(self.state, result):
                self.state.nested_parse(result, 0, node, match_titles=1)
        else:
            save = self.state.memo.title_styles, self.state.memo.section_level, self.state.memo.reporter
            self.state.memo.reporter = AutodocReporter(result, self.state.memo.reporter)
            self.state.memo.title_styles, self.state.memo.section_level = [], 0
            try:
                self.state.nested_parse(result, 0, node, match_titles=1)
            finally:
                self.state.memo.title_styles, self.state.memo.section_level, self.state.memo.reporter = save


def setup(app):
    app.add_config_value('kerneldoc_bin', None, 'env')
    app.add_config_value('kerneldoc_srctree', None, 'env')
    app.add_config_value('kerneldoc_verbosity', 1, 'env')

    app.add_directive('kernel-doc', KernelDocDirective)

    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )
