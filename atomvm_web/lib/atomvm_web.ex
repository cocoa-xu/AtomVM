defmodule AtomVMWeb do
  @moduledoc false

  @dists_dir Path.expand("dists")

  @doc """
  Returns the directory with AtomVM Web .js and .wasm files.
  """
  @spec dists_dir() :: String.t()
  def dists_dir(), do: @dists_dir
end
