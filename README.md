## 📚 References

### 📘 Books
- Introduction to 3D Game Programming with DirectX12 (Frank D. Luna)

### 📝 Documentation
- [Microsoft Direct3D 12 Documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/)
- [GPU Memory Pools in D3D12](https://therealmjp.github.io/posts/gpu-memory-pool/#how-it-works-in-d3d12)
- [What is the Base Address Register (BAR) in PCle?](https://stackoverflow.com/questions/30190050/what-is-the-base-address-register-bar-in-pcie)
- [어떻게 GPU는 DRAM에 있는 데이터에 접근하는가?](https://sungjjinkang.github.io/gpu_access_to_dram)

### 🔨 Build Demo
You can generate the solution files and build the project from the root directory of the source tree using the following CMake commands:

```bash
# Generate Visual Studio solution files
cmake -S. -B<build-directory> -G "Visual Studio 17 2022"
```

After generating the solution, you can build the project with:

```bash
cmake --build <build-directory> --config <Debug/Release>
```

---

