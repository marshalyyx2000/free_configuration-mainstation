"""Dump paragraphs and tables of a DOCX to UTF-8 text (for reading only)."""
import sys

from docx import Document
from docx.table import Table
from docx.text.paragraph import Paragraph


def iter_block_items(parent):
    from docx.oxml.ns import qn

    parent_elm = parent.element.body
    for child in parent_elm.iterchildren():
        if child.tag == qn("w:p"):
            yield Paragraph(child, parent)
        elif child.tag == qn("w:tbl"):
            yield Table(child, parent)


def main(path: str, out_path: str) -> None:
    doc = Document(path)
    lines = []
    for block in iter_block_items(doc):
        if isinstance(block, Paragraph):
            text = block.text.strip()
            if text:
                style = block.style.name if block.style is not None else ""
                lines.append(f"[{style}] {text}")
        else:
            lines.append("[TABLE]")
            for row in block.rows:
                cells = []
                for cell in row.cells:
                    cells.append(" ".join(p.text.strip() for p in cell.paragraphs if p.text.strip()))
                lines.append(" | ".join(cells))
            lines.append("[/TABLE]")
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    print(f"wrote {len(lines)} lines -> {out_path}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
