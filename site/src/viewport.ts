import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";
import type { DioramaGeometry, MeshData } from "./terrain/types.js";

function bufferGeometry(data: MeshData): THREE.BufferGeometry {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute(
    "position",
    new THREE.BufferAttribute(data.positions, 3),
  );
  geometry.setAttribute("color", new THREE.BufferAttribute(data.colors, 3));
  geometry.setIndex(new THREE.BufferAttribute(data.indices, 1));
  geometry.computeVertexNormals();
  return geometry;
}

/** A small studio viewport for a solid terrain specimen. */
export class Viewport {
  private readonly container: HTMLElement;
  private readonly renderer: THREE.WebGLRenderer;
  private readonly scene = new THREE.Scene();
  private readonly camera = new THREE.PerspectiveCamera(38, 1, 0.01, 100);
  private readonly controls: OrbitControls;
  private readonly resizeObserver: ResizeObserver;
  private readonly light: THREE.DirectionalLight;
  private readonly shadowPlane: THREE.Mesh;
  private readonly environment: THREE.Texture;

  private group: THREE.Group | null = null;
  private topMaterial: THREE.MeshStandardMaterial | null = null;
  private shellMaterial: THREE.MeshStandardMaterial | null = null;
  private frame = 0;
  private running = false;
  private active = true;
  private disposed = false;
  private wireframe = false;
  private flatShading = false;

  constructor(container: HTMLElement) {
    this.container = container;

    this.renderer = new THREE.WebGLRenderer({
      antialias: true,
      alpha: true,
      powerPreference: "high-performance",
    });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 0.9;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.domElement.setAttribute("role", "img");
    this.renderer.domElement.setAttribute(
      "aria-label",
      "Orbitable 3D terrain specimen",
    );
    container.append(this.renderer.domElement);

    const pmrem = new THREE.PMREMGenerator(this.renderer);
    const room = new RoomEnvironment();
    this.environment = pmrem.fromScene(room, 0.04).texture;
    room.dispose();
    pmrem.dispose();
    this.scene.environment = this.environment;

    this.light = new THREE.DirectionalLight(0xfff1dd, 1.35);
    this.light.castShadow = true;
    this.light.shadow.mapSize.set(2048, 2048);
    this.light.shadow.bias = -0.0002;
    this.light.shadow.radius = 4;
    this.scene.add(this.light);
    this.scene.add(this.light.target);
    this.scene.add(new THREE.HemisphereLight(0xfff6e8, 0x3b332b, 0.28));

    this.shadowPlane = new THREE.Mesh(
      new THREE.PlaneGeometry(1, 1),
      new THREE.ShadowMaterial({ color: 0x2a2118, opacity: 0.17 }),
    );
    this.shadowPlane.rotation.x = -Math.PI / 2;
    this.shadowPlane.receiveShadow = true;
    this.scene.add(this.shadowPlane);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.08;
    this.controls.enablePan = false;
    this.controls.minPolarAngle = 0.15;
    this.controls.maxPolarAngle = Math.PI / 2 - 0.06;
    this.controls.minDistance = 0.5;

    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(container);
    this.resize();
    this.start();
  }

  setTerrain(geometry: DioramaGeometry): void {
    if (this.disposed) return;
    this.clearTerrain();

    const topGeometry = bufferGeometry(geometry.top);
    const shellGeometry = bufferGeometry(geometry.shell);

    const topMaterial = new THREE.MeshStandardMaterial({
      vertexColors: true,
      roughness: 0.94,
      metalness: 0.02,
      envMapIntensity: 0.5,
      wireframe: this.wireframe,
      flatShading: this.flatShading,
    });
    const shellMaterial = new THREE.MeshStandardMaterial({
      vertexColors: true,
      roughness: 0.98,
      metalness: 0,
      envMapIntensity: 0.45,
      flatShading: true,
      wireframe: this.wireframe,
    });

    const top = new THREE.Mesh(topGeometry, topMaterial);
    top.castShadow = true;
    top.receiveShadow = true;
    const shell = new THREE.Mesh(shellGeometry, shellMaterial);
    shell.castShadow = true;
    shell.receiveShadow = true;

    const group = new THREE.Group();
    group.add(top, shell);
    this.scene.add(group);

    this.group = group;
    this.topMaterial = topMaterial;
    this.shellMaterial = shellMaterial;
    this.fit(geometry);
    this.renderOnce();
  }

  setActive(active: boolean): void {
    this.active = active;
    if (active) this.start();
    else this.stop();
  }

  setWireframe(enabled: boolean): void {
    this.wireframe = enabled;
    if (this.topMaterial) this.topMaterial.wireframe = enabled;
    if (this.shellMaterial) this.shellMaterial.wireframe = enabled;
    this.renderOnce();
  }

  setFlatShading(enabled: boolean): void {
    this.flatShading = enabled;
    if (this.topMaterial) {
      this.topMaterial.flatShading = enabled;
      this.topMaterial.needsUpdate = true;
    }
    this.renderOnce();
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.stop();
    this.resizeObserver.disconnect();
    this.controls.dispose();
    this.clearTerrain();
    this.shadowPlane.geometry.dispose();
    (this.shadowPlane.material as THREE.Material).dispose();
    this.environment.dispose();
    this.renderer.dispose();
    this.renderer.domElement.remove();
  }

  private fit(geometry: DioramaGeometry): void {
    if (!this.group) return;
    const box = new THREE.Box3().setFromObject(this.group);
    const sphere = box.getBoundingSphere(new THREE.Sphere());
    const radius = Math.max(sphere.radius, 0.5);
    const center = sphere.center.clone();

    this.shadowPlane.position.set(center.x, geometry.baseY - 0.002, center.z);
    this.shadowPlane.scale.setScalar(radius * 6.5);

    this.light.position.set(
      center.x + radius * 1.8,
      center.y + radius * 3.2,
      center.z + radius * 2.2,
    );
    this.light.target.position.copy(center);
    this.light.target.updateMatrixWorld();
    this.light.shadow.camera.left = -radius * 2.5;
    this.light.shadow.camera.right = radius * 2.5;
    this.light.shadow.camera.top = radius * 2.5;
    this.light.shadow.camera.bottom = -radius * 2.5;
    this.light.shadow.camera.near = 0.1;
    this.light.shadow.camera.far = radius * 12;
    this.light.shadow.camera.updateProjectionMatrix();

    this.camera.near = Math.max(radius / 100, 0.01);
    this.camera.far = radius * 30;
    this.camera.updateProjectionMatrix();

    const direction = new THREE.Vector3(1, 0.62, 1).normalize();
    this.camera.position
      .copy(center)
      .addScaledVector(direction, radius * 3.15);
    this.controls.target.copy(center);
    this.controls.minDistance = radius * 1.2;
    this.controls.maxDistance = radius * 7;
    this.controls.update();
  }

  private clearTerrain(): void {
    if (!this.group) return;
    this.scene.remove(this.group);
    this.group.traverse((object) => {
      if (object instanceof THREE.Mesh) {
        object.geometry.dispose();
        const material = object.material;
        if (Array.isArray(material)) {
          for (const entry of material) entry.dispose();
        } else {
          material.dispose();
        }
      }
    });
    this.group = null;
    this.topMaterial = null;
    this.shellMaterial = null;
  }

  private resize(): void {
    if (this.disposed) return;
    const width = this.container.clientWidth;
    const height = this.container.clientHeight;
    if (width === 0 || height === 0) return;
    this.renderer.setSize(width, height, false);
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this.renderOnce();
  }

  private renderOnce(): void {
    if (this.disposed) return;
    this.renderer.render(this.scene, this.camera);
  }

  private start(): void {
    if (this.running || this.disposed || !this.active) return;
    this.running = true;
    const tick = (): void => {
      if (!this.running || this.disposed || !this.active) {
        this.running = false;
        return;
      }
      this.controls.update();
      this.renderer.render(this.scene, this.camera);
      this.frame = requestAnimationFrame(tick);
    };
    this.frame = requestAnimationFrame(tick);
  }

  private stop(): void {
    this.running = false;
    cancelAnimationFrame(this.frame);
  }
}
