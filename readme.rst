==========================================
rtf2rtf: convert rtf to human friendly rtf
==========================================

Motivation
----------

Look this RTF file.

.. image:: https://raw.github.com/ytomino/rtf2rtf/master/doc-textedit.png

The source of this RTF is difficult to read.

.. image:: https://raw.github.com/ytomino/rtf2rtf/master/doc-before.png

*rtf2rtf* helps to read it a little.

.. image:: https://raw.github.com/ytomino/rtf2rtf/master/doc-after.png

Note, the correct encoding of RTF is ASCII.
UTF-8 (by the option ``-t UTF-8`` in this screen-shot) is illegal,
and should be used for reading only.

Settings for git diff
---------------------

Insert into ``.gitattributes``: ::

 *.rtf diff=rtf

Insert into ``.gitconfig``: ::

 [diff "rtf"]
 	textconv = rtf2rtf -t utf-8
