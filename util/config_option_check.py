#!/usr/bin/python2
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Configuration Option Checker.

Script to ensure that all configuration options for the Chrome EC are defined
in config.h.
"""
from __future__ import print_function
import re
import os
import argparse

def find_files_to_check(args):
  """Returns a list of files to check."""
  file_list = []
  if args.all_files:
    cwd = os.getcwd()
    for (dirpath, dirnames, filenames) in os.walk(cwd, topdown=True):
      # Ignore the build and private directories (taken from .gitignore)
      if 'build' in dirnames:
        dirnames.remove('build')
      if 'private' in dirnames:
        dirnames.remove('private')
      for f in filenames:
        # Only consider C source and assembler files.
        if f.endswith(('.c', '.h', '.inc', '.S')):
          file_list.append(os.path.join(dirpath, f))
  else:
    # Form list from presubmit environment variable.
    file_list = os.environ['PRESUBMIT_FILES'].split()
  return file_list

def obtain_current_config_options():
  """Obtains current config options from include/config.h"""
  config_options = []
  config_option_re = re.compile(r'\s+(CONFIG_[a-zA-Z0-9_]*)\s*')
  with open('include/config.h', 'r') as config_file:
    for line in config_file:
      match = re.search(config_option_re, line)
      if match:
        if match.group(1) not in config_options:
          config_options.append(match.group(1))
  return config_options

def print_missing_config_options(file_list, config_options):
  """Searches through all files in file_list for missing config options."""
  missing_config_option = False
  print_banner = True
  # Determine longest CONFIG_* length to be used for formatting.
  max_option_length = max(len(option) for option in config_options)
  config_option_re = re.compile(r'\s+(CONFIG_[a-zA-Z0-9_]*)\s*')
  for f in file_list:
    with open(f, 'r') as cur_file:
      line_num = 0
      for line in cur_file:
        line_num += 1
        match = re.search(config_option_re, line)
        if match:
          if match.group(1) not in config_options:
            missing_config_option = True
            # Print the banner once.
            if print_banner:
              print('Please add all new config options to include/config.h' \
                    ' along with a description of the option.\n\n' \
                    'The following config options were found to be missing ' \
                    'from include/config.h.\n')
              print_banner = False
            # Print the misssing config option.
            print('> %-*s %s:%s' % (max_option_length, match.group(1), f,
                                    line_num))
  return missing_config_option

def main():
  """Searches through specified source files for missing config options.

  Checks through specified C source and assembler file (not in the build/ and
  private/ directories) for CONFIG_* options.  Then checks to make sure that
  all CONFIG_* options are defined in include/config.h.  Finally, reports any
  missing config options.  By default, it will check just the files in the CL.
  To check all files in EC code base, run with the flag --all_files.
  """
  # Create argument options to specify checking either all the files in the EC
  # code base or just the files in the CL.
  parser = argparse.ArgumentParser(description='configuration option checker.')
  parser.add_argument('--all_files', help='check all files in EC code base',
                      action='store_true')
  args = parser.parse_args()

  # Create list of files to search.
  file_list = find_files_to_check(args)
  # Obtain config options from include/config.h.
  config_options = obtain_current_config_options()
  # Find any missing config options from file list and print them.
  missing_opts = print_missing_config_options(file_list, config_options)

  if missing_opts:
    print('\nIt may also be possible that you have a typo.')
    os.sys.exit(1)

if __name__ == '__main__':
  main()
