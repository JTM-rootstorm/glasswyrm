tool_sources = {
  'gwctl': 'gwctl/main.cpp',
  'gwinfo': 'gwinfo/main.cpp',
  'gwtrace': 'gwtrace/main.cpp',
  'gwout': 'gwout/main.cpp',
  'gwbench': 'gwbench/main.cpp',
}

foreach tool_name, tool_source : tool_sources
  executable(
    tool_name,
    files(tool_source),
    dependencies: gw_scaffold_dep,
    install: true,
  )
endforeach
