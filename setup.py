from setuptools import setup, find_packages
import os
import re

module_name = "rfsoc_moller"

def find_version(file_path):
    with open(file_path, "r", encoding="utf-8") as fp:
        contents = fp.read()
    match = re.search(r"^__version__ = ['\"]([^'\"]*)['\"]", contents, re.M)
    if match:
        return match.group(1)
    raise RuntimeError("Unable to find __version__ string.")

setup(
    name=module_name,
    version=find_version(os.path.join(module_name, "__init__.py")),
    description="MOLLER RFSoC software package",
    packages=find_packages(include=[module_name, f"{module_name}.*"]),
    python_requires=">=3.10.0",
    install_requires=[
        "pynq",
        "numpy",
    ],
)