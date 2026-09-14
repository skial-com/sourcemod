# vim: set ts=8 sts=2 sw=2 tw=99 et:
import re
import os, sys
import subprocess

argv = sys.argv[1:]
if len(argv) < 2:
  sys.stderr.write('Usage: generate_headers.py <source_path> <output_folder>\n')
  sys.exit(1)

SourceFolder = os.path.abspath(os.path.normpath(argv[0]))
OutputFolder = os.path.normpath(argv[1])

class FolderChanger:
  def __init__(self, folder):
    self.old = os.getcwd()
    self.new = folder

  def __enter__(self):
    if self.new:
      os.chdir(self.new)

  def __exit__(self, type, value, traceback):
    os.chdir(self.old)

def run_and_return(argv):
  # Python 2.6 doesn't have check_output.
  if hasattr(subprocess, 'check_output'):
    text = subprocess.check_output(argv)
    if str != bytes:
      text = str(text, 'utf-8')
  else:
    p = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output, ignored = p.communicate()
    rval = p.poll()
    if rval:
      raise subprocess.CalledProcessError(rval, argv)
    text = output.decode('utf8')
  return text.strip()

def get_git_version():
  revision_count = run_and_return(['git', 'rev-list', '--count', 'HEAD'])
  revision_hash = run_and_return(['git', 'log', '--pretty=format:%h:%H', '-n', '1'])
  shorthash, longhash = revision_hash.split(':')

  return revision_count, shorthash, longhash

def get_upstream_version():
  # The nearest upstream release tag (e.g. "1.13.0.7461") and how many local
  # commits sit on top of it. Upstream release tags look like N.N.N.N; the
  # match pattern deliberately ignores local/vendor tags such as "skial-*" or
  # "production-*" so this reports the last merged upstream build, not ours.
  try:
    desc = run_and_return(['git', 'describe', '--long', '--tags',
                           '--match', '[0-9]*.[0-9]*.[0-9]*.[0-9]*', 'HEAD'])
  except Exception:
    return '', '0'
  m = re.match(r'^(.+)-(\d+)-g[0-9A-Fa-f]+$', desc)
  if m is None:
    return '', '0'
  return m.group(1), m.group(2)

def get_upstream_sha(tag):
  # SHA of the last upstream commit this build sits on. Prefer the merge-base
  # with a known upstream tracking ref (accurate even if upstream commits past
  # the last release tag were merged); fall back to the commit the nearest
  # upstream release tag points at (always in the repo, needs no remote). The
  # result is a commit in alliedmodders/sourcemod history, so it resolves there.
  def short(sha):
    try:
      return run_and_return(['git', 'rev-parse', '--short', sha])
    except Exception:
      return sha[:9]
  for ref in ['upstream/master', 'upstream/HEAD']:
    try:
      base = run_and_return(['git', 'merge-base', 'HEAD', ref])
      if base:
        return short(base)
    except Exception:
      pass
  if tag:
    try:
      return short(run_and_return(['git', 'rev-parse', tag + '^{commit}']))
    except Exception:
      pass
  return ''

def output_version_headers():
  with FolderChanger(SourceFolder):
    count, shorthash, longhash = get_git_version()
    upstream, upstream_ahead = get_upstream_version()
    upstream_sha = get_upstream_sha(upstream)

  with open(os.path.join(SourceFolder, 'product.version')) as fp:
    contents = fp.read().strip()
  m = re.match(r'(\d+)\.(\d+)\.(\d+)-?(.*)', contents)
  if m == None:
    raise Exception('Could not detremine product version')
  major, minor, release, tag = m.groups()
  product = "{0}.{1}.{2}.{3}".format(major, minor, release, count)
  fullstring = product
  if tag != "":
    fullstring += "-{0}".format(tag)

  with open(os.path.join(OutputFolder, 'sourcemod_version_auto.h'), 'w') as fp:
    fp.write("""
#ifndef _SOURCEMOD_AUTO_VERSION_INFORMATION_H_
#define _SOURCEMOD_AUTO_VERSION_INFORMATION_H_

#define SM_BUILD_TAG		\"{0}\"
#define SM_BUILD_CSET		\"{1}\"
#define SM_BUILD_MAJOR		\"{2}\"
#define SM_BUILD_MINOR		\"{3}\"
#define SM_BUILD_RELEASE	\"{4}\"
#define SM_BUILD_LOCAL_REV      \"{6}\"
#define SM_BUILD_UPSTREAM       \"{7}\"
#define SM_BUILD_UPSTREAM_AHEAD \"{8}\"
#define SM_BUILD_UPSTREAM_SHA   \"{9}\"

#define SM_BUILD_UNIQUEID       SM_BUILD_LOCAL_REV \":\" SM_BUILD_CSET

#define SM_VERSION_STRING	\"{5}\"
#define SM_VERSION_FILE		{2},{3},{4},{6}

#endif /* _SOURCEMOD_AUTO_VERSION_INFORMATION_H_ */
    """.format(tag, shorthash, major, minor, release, fullstring, count, upstream, upstream_ahead, upstream_sha))

  with open(os.path.join(OutputFolder, 'version_auto.inc'), 'w') as fp:
    fp.write("""
#if defined _auto_version_included
 #endinput
#endif
#define _auto_version_included

#define SOURCEMOD_V_TAG		\"{0}\"
#define SOURCEMOD_V_CSET	\"{1}\"
#define SOURCEMOD_V_MAJOR	{2}
#define SOURCEMOD_V_MINOR	{3}
#define SOURCEMOD_V_RELEASE	{4}
#define SOURCEMOD_V_REV		{6}

#define SOURCEMOD_VERSION	\"{5}\"
    """.format(tag, shorthash, major, minor, release, fullstring, count))

output_version_headers()
