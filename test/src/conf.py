from configparser import ExtendedInterpolation
import getpass
from os.path import abspath, realpath, dirname, basename
from configparser import ConfigParser, ExtendedInterpolation

FILEDIR: str = abspath(dirname(realpath(__file__)))
PDIR: str = dirname(dirname(FILEDIR)) # this file is expected to be at ./*/*/autotest.py relative to project root
PDIR_PARENT: str = dirname(PDIR)
PDIR_NAME: str = basename(PDIR)
USERNAME: str = getpass.getuser()
CONFIG_DEFAULTS = { 
        "projectDirectory": PDIR, 
        "projectDirectoryName": PDIR_NAME,
        "projectDirectoryParent": PDIR_PARENT,
        "username": USERNAME 
       }

def default_config_parser() -> ConfigParser:
    return ConfigParser(defaults=CONFIG_DEFAULTS, interpolation=ExtendedInterpolation())
