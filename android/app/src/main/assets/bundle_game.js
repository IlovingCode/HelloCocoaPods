const gameRoot = new Scene
const LIGHT_SCALE = 20000

gameRoot.textures = {
    bg0: {
        width: 1024,
        height: 768,
        native: null
    },
    bg1: {
        width: 1024,
        height: 768,
        native: null
    },
    bg2: {
        width: 1024,
        height: 768,
        native: null
    },
    bg3: {
        width: 1024,
        height: 768,
        native: null
    }
}

gameRoot.models = {
    amongus: {
        native: null,
        count: 0,
    },
    cube: {
        native: null,
        count: 0,
    }
}

let gameController = null

class GameController extends Component {
    constructor(node, textures, character) {
        super(node)

        // this.timer = 0
        this.origin = Vec2.ZERO.clone()
        this.dir = Vec2.ZERO.clone()
        gameRoot.interactables.push(this)

        // let { bg0, bg1, bg2, bg3 } = textures

        // let y = 0
        // let child = node.addChild()
        // child.position.y = y
        // new BoundBox2D(child, new Vec2(bg0.width, bg0.height), Vec2.ZERO.clone())
        // new SpriteSimple(child, bg0)
        // y += bg0.height

        // child = node.addChild()
        // child.position.y = y
        // new BoundBox2D(child, new Vec2(bg1.width, bg1.height), Vec2.ZERO.clone())
        // new SpriteSimple(child, bg1)
        // y += bg1.height

        // child = node.addChild()
        // child.position.y = y
        // new BoundBox2D(child, new Vec2(bg2.width, bg2.height), Vec2.ZERO.clone())
        // new SpriteSimple(child, bg2)
        // y += bg2.height

        // child = node.addChild()
        // child.position.y = y
        // new BoundBox2D(child, new Vec2(bg3.width, bg3.height), Vec2.ZERO.clone())
        // new SpriteSimple(child, bg3)

        // this.height = y + bg3.height
        // this.width = bg0.width

        this.character = character
        this.angle = 0
    }

    update(dt) {
        this.angle += dt * .3

        let r = 8
        let node = this.character

        node.position.x = Math.cos(this.angle) * r
        node.position.z = Math.sin(this.angle) * r
        node.rotation.y = -this.angle

        node.isDirty = true
    }

    clear() {
        for (let i of this.node.children) i.destroy()
        this.node.children.length = 0
    }

    check(x, y, state) {
        let origin = this.origin
        if (state == 0) {
            // origin.set(x, y)
            // return
        }

        if (state == 3) {
            this.spine.play('idle')
            return
        }

        this.onDirection(this.dir.set(x - origin.x, y - origin.y))
    }

    onDirection(dir) {
        let length = Math.min(Math.sqrt(dir.lengthSqr()), 5)
        if (length <= 0) return

        dir.normalize()

        if(Math.abs(dir.x) < Math.abs(dir.y)) {
            this.spine.play(dir.y > 0 ? 'up' : 'down')
        } else {
            this.spine.play(dir.x < 0 ? 'right' : 'left')
        }

        // let { width, height, scale } = gameRoot.camera
        // let node = gameRoot.camera.node
        // let pos = node.position
        // pos.x += dir.x * length
        // pos.y += dir.y * length

        // width *= scale * .5
        // height *= scale * .5

        // if (pos.x < width) pos.x = width
        // if (pos.y < height) pos.y = height

        // width = this.width - width
        // height = this.height - height
        // if (pos.x > width) pos.x = width
        // if (pos.y > height) pos.y = height

        // node.isDirty = true
    }
}

void function init() {
    let { textures } = gameRoot.init()

    // let node = gameRoot.addChild()
    // let pos = node.position
    // pos.z = 1
    // pos.x = textures.bg0.width * .25
    // pos.y = textures.bg0.height * .35
    // new Camera(node).scale = .5

    let modelview = gameRoot.importNodesFromModel(gameRoot.models.amongus)

    gameRoot.light && globalThis.updateLight(gameRoot.light.id(), modelview.data.lightIntensity * LIGHT_SCALE)
    gameRoot.camera.scale = .5

    modelview.play('ArmatureAction', true)

    node = gameRoot.addChild()
    gameController = new GameController(node, textures, modelview.getNodeByName('Armature'))

    gameRoot.setEnvironment({
        skybox: 'env/env_skybox.ktx',
        ibl: 'env/env_ibl.ktx',
        indirect_intensity: LIGHT_SCALE,
    })

    // for(let i = 0; i < 5; i++) {
    //     for(let j = 0; j < 5; j++) {
    //         let cube = gameRoot.importNodesFromModel(gameRoot.models.cube)
    //         cube.node.position.x = (i - 3) * 3
    //         cube.node.position.z = (j - 3) * 3
    //         cube.node.position.y = 3
    //         cube.node.isDirty = true
    //     }
    // }

    // console.log(JSON.stringify(cube.data))

    // globalThis.beginPhysics()

    // node = modelview.getNodeByName('Cylinder')
    // new RigidBody(node, 0, PhysicsShape.CYLINDERY, 120, 10, 120)
}();

