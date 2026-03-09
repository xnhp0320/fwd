1. The best practice for DPDK is not use bazel but pre-installed as there are many reasons:
   * the vendor has its own DPDK, which is not public available to use.
   * the CPUs are different from your local bazel build.
   * the meson is not working, this is the worst, for Apple M4 CPUS, `-march=native` is not accpeted by GCC.
2. However, uses bazel has good reasons, even for personal use:
   * The blzmod is actually a C++ package center with boost, abseil, and other famouse C++ lib.

## LSP support for Bazel C/C++ (vim-lsp + clangd)

This repo includes a Bazel-based compile database generator:

```bash
./generate_compile_commands.py
```

It runs `bazel aquery` for C++ compile actions and writes:

```text
./compile_commands.json
```

You should rerun it after:
- BUILD / MODULE changes
- adding/removing C++ source files
- changing compile flags/toolchains

You can also scope it to specific targets:

```bash
./generate_compile_commands.py //:main //control:command_handler_test
```

### vim-lsp setup for clangd

Add this to your `~/.vimrc` (or vim config file):

```vim
if executable('clangd')
  augroup lsp_cpp
    autocmd!
    autocmd User lsp_setup call lsp#register_server({
      \ 'name': 'clangd',
      \ 'cmd': {server_info->[
      \   'clangd',
      \   '--background-index',
      \   '--clang-tidy',
      \   '--completion-style=detailed'
      \ ]},
      \ 'allowlist': ['c', 'cpp', 'objc', 'objcpp'],
      \ })
  augroup END
endif

function! s:on_lsp_buffer_enabled() abort
  setlocal omnifunc=lsp#complete
  setlocal signcolumn=yes
  if exists('+tagfunc') | setlocal tagfunc=lsp#tagfunc | endif
  nmap <buffer> gd <plug>(lsp-definition)
  nmap <buffer> gr <plug>(lsp-references)
  nmap <buffer> K <plug>(lsp-hover)
  nmap <buffer> [g <plug>(lsp-previous-diagnostic)
  nmap <buffer> ]g <plug>(lsp-next-diagnostic)
endfunction

augroup lsp_install
  autocmd!
  autocmd User lsp_buffer_enabled call s:on_lsp_buffer_enabled()
augroup END

" Optional helper to refresh compile_commands.json from inside Vim.
command! BazelRefreshCompileDB !./generate_compile_commands.py
```

`clangd` auto-detects `compile_commands.json` in the workspace root.

If `clangd` cannot jump to definitions in external libs (abseil/boost/etc.):
- regenerate the DB: `./generate_compile_commands.py`
- restart clangd (or restart Vim)
- confirm the first entry in `compile_commands.json` has `"directory"` under Bazel `execution_root` (not workspace root)
