# Table C.1 Extraction Report

Source PDF: `references/表 C.1 极化码的可靠度排序序列.pdf`

Extraction checks:

- raw `pdftotext` path parsed successfully
- layout `pdftotext` path parsed successfully
- raw/layout sequences matched exactly
- parsed rows: 1024
- parsed `(W, Q)` pairs: 8192
- `W` coverage: `0..8191`, no missing values
- `Q` coverage: `0..8191`, no missing values and no duplicates

Sentinel values:

- first 16 Q values: `0, 1, 2, 4, 8, 16, 32, 3, 5, 64, 9, 6, 17, 10, 18, 128`
- last 16 Q values: `8179, 8181, 7935, 8182, 8185, 8063, 8186, 8183, 8188, 8187, 8175, 8127, 8190, 8191, 8159, 8189`
