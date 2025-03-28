defmodule AtomVMWeb.MixProject do
  use Mix.Project

  @version "VERSION_PLACEHOLDER"
  if @version == "VERSION_PLACEHOLDER" do
    raise """
    Please replace VERSION_PLACEHOLDER with the actual version of the embedded AtomVM files.
    """
  end

  @description "AtomVM Web dists"
  @github_url "https://github.com/cocoa-xu/AtomVM"

  def project do
    [
      app: :atomvm_web,
      version: @version,
      elixir: "~> 1.15",
      name: "AtomVM Web Distribution",
      description: @description,
      start_permanent: Mix.env() == :prod,
      deps: deps(),
      docs: docs(),
      package: package()
    ]
  end

  def application do
    []
  end

  defp deps do
    [
      {:ex_doc, "~> 0.37", only: [:dev, :docs], runtime: false}
    ]
  end

  defp docs() do
    [
      main: "AtomVM Web",
      source_url: @github_url,
      source_ref: "v#{@version}"
    ]
  end

  defp package do
    [
      licenses: ["Apache-2.0"],
      links: %{"GitHub" => @github_url},
      files: ~w(dists lib dists/*.sha256 dists/*.js dists/*.wasm mix.exs README.md)
    ]
  end
end
